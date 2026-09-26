#include "zenz_scorer/synchronous_io_deadline.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "testing/gunit.h"

namespace mozc::zenz {
namespace {

bool ConnectOverlappedPipe(HANDLE pipe) {
  HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (event == nullptr) {
    return false;
  }

  OVERLAPPED overlapped = {};
  overlapped.hEvent = event;
  if (::ConnectNamedPipe(pipe, &overlapped)) {
    ::CloseHandle(event);
    return true;
  }

  const DWORD error = ::GetLastError();
  if (error == ERROR_PIPE_CONNECTED) {
    ::CloseHandle(event);
    return true;
  }
  if (error != ERROR_IO_PENDING ||
      ::WaitForSingleObject(event, 2000) != WAIT_OBJECT_0) {
    ::CancelIoEx(pipe, &overlapped);
    DWORD ignored = 0;
    ::GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
    ::CloseHandle(event);
    return false;
  }

  DWORD ignored = 0;
  const bool connected =
      ::GetOverlappedResult(pipe, &overlapped, &ignored, FALSE);
  ::CloseHandle(event);
  return connected;
}

void CheckStalledPeer(bool flush) {
  static std::atomic<int> sequence{0};
  const std::wstring name = L"\\\\.\\pipe\\mozc_deadline_test_" +
                            std::to_wstring(::GetCurrentProcessId()) + L"_" +
                            std::to_wstring(sequence.fetch_add(1));
  const DWORD pipe_access =
      PIPE_ACCESS_DUPLEX | (flush ? 0 : FILE_FLAG_OVERLAPPED);
  HANDLE pipe = ::CreateNamedPipeW(
      name.c_str(), pipe_access, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
      nullptr);
  ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
  std::atomic<bool> done{false};
  std::thread peer([&] {
    HANDLE client = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
    if (client != INVALID_HANDLE_VALUE) {
      // Finite fallback makes a broken watchdog fail instead of hanging tests.
      for (int i = 0; i < 3000 && !done.load(); ++i) {
        ::Sleep(1);
      }
      ::CloseHandle(client);
    }
  });
  const bool connected = flush ? (::ConnectNamedPipe(pipe, nullptr) ||
                                  ::GetLastError() == ERROR_PIPE_CONNECTED)
                               : ConnectOverlappedPipe(pipe);
  EXPECT_TRUE(connected);
  if (connected) {
    char byte = 'x';
    if (flush) {
      DWORD transferred = 0;
      EXPECT_TRUE(::WriteFile(pipe, &byte, 1, &transferred, nullptr));
    }
    const auto started = std::chrono::steady_clock::now();
    if (flush) {
      SynchronousIoDeadline deadline(pipe, std::chrono::milliseconds(50));
      EXPECT_TRUE(deadline.valid());
      const BOOL ok = ::FlushFileBuffers(pipe);
      const DWORD error = ::GetLastError();
      EXPECT_FALSE(ok);
      EXPECT_EQ(error, ERROR_OPERATION_ABORTED);
    } else {
      HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      EXPECT_NE(event, nullptr);
      if (event != nullptr) {
        OVERLAPPED overlapped = {};
        overlapped.hEvent = event;
        SynchronousIoDeadline deadline(pipe, std::chrono::milliseconds(50));
        EXPECT_TRUE(deadline.valid());
        const BOOL started_read =
            ::ReadFile(pipe, &byte, 1, nullptr, &overlapped);
        const DWORD read_error = ::GetLastError();
        if (started_read || read_error != ERROR_IO_PENDING) {
          ADD_FAILURE() << "ReadFile did not start an overlapped operation: "
                        << read_error;
        } else {
          const DWORD wait = ::WaitForSingleObject(event, 2000);
          EXPECT_EQ(wait, WAIT_OBJECT_0);
          if (wait == WAIT_OBJECT_0) {
            DWORD transferred = 0;
            const BOOL completed =
                ::GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
            const DWORD error = ::GetLastError();
            EXPECT_FALSE(completed);
            EXPECT_EQ(error, ERROR_OPERATION_ABORTED);
          } else {
            ::CancelIoEx(pipe, &overlapped);
            DWORD transferred = 0;
            ::GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
            ADD_FAILURE() << "overlapped read was not cancelled in time";
          }
        }
        ::CloseHandle(event);
      }
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started,
              std::chrono::seconds(2));
  }
  done = true;
  peer.join();
  ::DisconnectNamedPipe(pipe);
  ::CloseHandle(pipe);
}

TEST(SynchronousIoDeadlineTest, CancelsStalledOverlappedRead) {
  CheckStalledPeer(false);
}

TEST(SynchronousIoDeadlineTest, CancelsUnreadResponseFlush) {
  CheckStalledPeer(true);
}

TEST(SynchronousIoDeadlineTest, CompletedOperationDoesNotWaitForDeadline) {
  const auto started = std::chrono::steady_clock::now();
  {
    SynchronousIoDeadline deadline(std::chrono::seconds(30));
    EXPECT_TRUE(deadline.valid());
  }
  EXPECT_LT(std::chrono::steady_clock::now() - started,
            std::chrono::seconds(2));
}

}  // namespace
}  // namespace mozc::zenz
