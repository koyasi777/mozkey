#include "session/zenz_named_pipe_client.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "testing/gunit.h"
#include "zenz/zenz_wire_protocol.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mozc {
namespace session {
namespace {

#if defined(_WIN32)

bool AllowSyntheticPipePeer(void* handle) {
  return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

using ::mozc::zenz::kZenzWireKindRequest;
using ::mozc::zenz::kZenzWireKindResponse;
using ::mozc::zenz::kZenzWireMagic;
using ::mozc::zenz::kZenzWireStatusOk;
using ::mozc::zenz::kZenzWireVersion;
using ::mozc::zenz::ZenzWireRequestHeader;
using ::mozc::zenz::ZenzWireResponseHeader;

struct TestPipeName {
  std::string utf8;
  std::wstring wide;
};

TestPipeName MakeTestPipeName() {
  static std::atomic<uint32_t> sequence{0};
  const std::string suffix =
      "mozc_zenz_named_pipe_client_test_" +
      std::to_string(::GetCurrentProcessId()) + "_" +
      std::to_string(::GetTickCount64()) + "_" +
      std::to_string(sequence.fetch_add(1));
  TestPipeName result;
  result.utf8 = "\\\\.\\pipe\\" + suffix;
  result.wide = L"\\\\.\\pipe\\" + std::wstring(suffix.begin(), suffix.end());
  return result;
}

HANDLE CreateServerPipe(const std::wstring& name) {
  return ::CreateNamedPipeW(
      name.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
      nullptr);
}

bool ConnectServer(HANDLE pipe) {
  if (::ConnectNamedPipe(pipe, nullptr)) {
    return true;
  }
  return ::GetLastError() == ERROR_PIPE_CONNECTED;
}

bool ReadExact(HANDLE pipe, void* data, uint32_t size) {
  uint8_t* current = static_cast<uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    DWORD read = 0;
    if (!::ReadFile(pipe, current, remaining, &read, nullptr) || read == 0) {
      return false;
    }
    current += read;
    remaining -= read;
  }
  return true;
}

bool WriteExact(HANDLE pipe, const void* data, uint32_t size) {
  const uint8_t* current = static_cast<const uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    DWORD written = 0;
    if (!::WriteFile(pipe, current, remaining, &written, nullptr) ||
        written == 0) {
      return false;
    }
    current += written;
    remaining -= written;
  }
  return true;
}

bool ReadRequest(HANDLE pipe, ZenzWireRequestHeader* header,
                 std::string* prompt) {
  if (!ReadExact(pipe, header, sizeof(*header))) {
    return false;
  }
  prompt->assign(header->prompt_size, '\0');
  return prompt->empty() ||
         ReadExact(pipe, prompt->data(), header->prompt_size);
}

ZenzLiveRequest MakeRequest(const TestPipeName& name, uint32_t generation) {
  ZenzLiveRequest request;
  request.generation = generation;
  request.key = "てすと";
  request.prompt = "prompt-data";
  request.pipe_name = name.utf8;
  request.timeout_msec = 180;
  request.max_output_chars = 48;
  return request;
}

TEST(ZenzNamedPipeClientTest, StopInterruptsMissingResponseAndJoinsWorker) {
  const TestPipeName name = MakeTestPipeName();
  HANDLE server = CreateServerPipe(name.wide);
  ASSERT_NE(server, INVALID_HANDLE_VALUE);
  std::atomic<bool> received{false};
  std::atomic<bool> done{false};
  std::thread server_thread([&] {
    ZenzWireRequestHeader request = {};
    std::string prompt;
    if (ConnectServer(server) && ReadRequest(server, &request, &prompt)) {
      received = true;
      for (int i = 0; i < 5000 && !done.load(); ++i) {
        ::Sleep(1);
      }
    }
    ::CloseHandle(server);
  });
  auto client = std::make_unique<ZenzNamedPipeClient>(
      ZenzNamedPipeClient::kDefaultTransportSafetyTimeoutMsec,
      &AllowSyntheticPipePeer);
  auto* client_ptr = client.get();
  ZenzLiveCorrector corrector(std::move(client));
  corrector.Submit(MakeRequest(name, 101));
  for (int i = 0; i < 2000 && !received.load(); ++i) {
    ::Sleep(1);
  }
  const auto started = std::chrono::steady_clock::now();
  corrector.Stop();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  done = true;
  server_thread.join();
  EXPECT_TRUE(received.load());
  EXPECT_LT(elapsed, std::chrono::seconds(2));

  EXPECT_FALSE(corrector.TakeResult(101).has_value());
  EXPECT_TRUE(client_ptr->IsStopRequested());
  corrector.Start();
  EXPECT_FALSE(client_ptr->IsStopRequested());
  corrector.Stop();
}

TEST(ZenzNamedPipeClientTest, PreCancelledRequestDoesNotLaunchOrConnect) {
  ZenzNamedPipeClient client;
  client.RequestStop();
  const auto started = std::chrono::steady_clock::now();
  EXPECT_FALSE(client.Convert(MakeRequest(MakeTestPipeName(), 102)).ok);
  EXPECT_LT(std::chrono::steady_clock::now() - started,
            std::chrono::seconds(1));
}

TEST(ZenzNamedPipeClientTest, ImmediateFullResponseSucceeds) {
  const TestPipeName name = MakeTestPipeName();
  HANDLE server = CreateServerPipe(name.wide);
  ASSERT_NE(server, INVALID_HANDLE_VALUE);

  std::atomic<bool> client_done{false};
  std::atomic<bool> server_ok{false};
  std::thread server_thread([&] {
    ZenzWireRequestHeader request = {};
    std::string prompt;
    if (!ConnectServer(server) || !ReadRequest(server, &request, &prompt)) {
      ::CloseHandle(server);
      return;
    }

    const std::string value = "converted";
    const std::string debug = "test_backend";
    ZenzWireResponseHeader response = {};
    response.magic = kZenzWireMagic;
    response.version = kZenzWireVersion;
    response.kind = kZenzWireKindResponse;
    response.generation = request.generation;
    response.status = kZenzWireStatusOk;
    response.latency_msec = 12;
    response.value_size = static_cast<uint32_t>(value.size());
    response.debug_size = static_cast<uint32_t>(debug.size());

    server_ok =
        request.magic == kZenzWireMagic &&
        request.version == kZenzWireVersion &&
        request.kind == kZenzWireKindRequest &&
        prompt == "prompt-data" &&
        WriteExact(server, &response, sizeof(response)) &&
        WriteExact(server, value.data(), response.value_size) &&
        WriteExact(server, debug.data(), response.debug_size);

    for (int i = 0; server_ok.load() && !client_done.load() && i < 2000; ++i) {
      ::Sleep(1);
    }
    ::CloseHandle(server);
  });

  ZenzNamedPipeClient client(1000, &AllowSyntheticPipePeer);
  const ZenzLiveResponse response = client.Convert(MakeRequest(name, 73));
  client_done = true;
  server_thread.join();

  EXPECT_TRUE(server_ok.load());
  EXPECT_TRUE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.generation, 73);
  EXPECT_EQ(response.value, "converted");
  EXPECT_EQ(response.debug, "test_backend");
}

TEST(ZenzNamedPipeClientTest, MissingResponseHeaderTimesOut) {
  const TestPipeName name = MakeTestPipeName();
  HANDLE server = CreateServerPipe(name.wide);
  ASSERT_NE(server, INVALID_HANDLE_VALUE);

  std::atomic<bool> server_received{false};
  std::thread server_thread([&] {
    ZenzWireRequestHeader request = {};
    std::string prompt;
    if (ConnectServer(server) && ReadRequest(server, &request, &prompt)) {
      server_received = true;
      ::Sleep(500);
    }
    ::CloseHandle(server);
  });

  ZenzNamedPipeClient client(200, &AllowSyntheticPipePeer);
  const auto started = std::chrono::steady_clock::now();
  const ZenzLiveResponse response = client.Convert(MakeRequest(name, 74));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  server_thread.join();

  EXPECT_TRUE(server_received.load());
  EXPECT_FALSE(response.ok);
  EXPECT_TRUE(response.timeout);
  EXPECT_EQ(response.debug, "pipe_read_header_timeout");
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
            2000);
}

TEST(ZenzNamedPipeClientTest, PartialResponsePayloadTimesOut) {
  const TestPipeName name = MakeTestPipeName();
  HANDLE server = CreateServerPipe(name.wide);
  ASSERT_NE(server, INVALID_HANDLE_VALUE);

  std::atomic<bool> header_written{false};
  std::thread server_thread([&] {
    ZenzWireRequestHeader request = {};
    std::string prompt;
    if (!ConnectServer(server) || !ReadRequest(server, &request, &prompt)) {
      ::CloseHandle(server);
      return;
    }

    ZenzWireResponseHeader response = {};
    response.magic = kZenzWireMagic;
    response.version = kZenzWireVersion;
    response.kind = kZenzWireKindResponse;
    response.generation = request.generation;
    response.status = kZenzWireStatusOk;
    response.value_size = 8;
    header_written = WriteExact(server, &response, sizeof(response));
    ::Sleep(500);
    ::CloseHandle(server);
  });

  ZenzNamedPipeClient client(200, &AllowSyntheticPipePeer);
  const ZenzLiveResponse response = client.Convert(MakeRequest(name, 75));
  server_thread.join();

  EXPECT_TRUE(header_written.load());
  EXPECT_FALSE(response.ok);
  EXPECT_TRUE(response.timeout);
  EXPECT_EQ(response.debug, "pipe_read_payload_timeout");
}

TEST(ZenzNamedPipeClientTest, OversizedResponsePayloadIsRejected) {
  const TestPipeName name = MakeTestPipeName();
  HANDLE server = CreateServerPipe(name.wide);
  ASSERT_NE(server, INVALID_HANDLE_VALUE);

  std::atomic<bool> header_written{false};
  std::thread server_thread([&] {
    ZenzWireRequestHeader request = {};
    std::string prompt;
    if (!ConnectServer(server) || !ReadRequest(server, &request, &prompt)) {
      ::CloseHandle(server);
      return;
    }

    ZenzWireResponseHeader response = {};
    response.magic = kZenzWireMagic;
    response.version = kZenzWireVersion;
    response.kind = kZenzWireKindResponse;
    response.generation = request.generation;
    response.status = kZenzWireStatusOk;
    response.value_size = 1024 * 1024 + 1;
    header_written = WriteExact(server, &response, sizeof(response));
    ::Sleep(50);
    ::CloseHandle(server);
  });

  ZenzNamedPipeClient client(500, &AllowSyntheticPipePeer);
  const ZenzLiveResponse response = client.Convert(MakeRequest(name, 76));
  server_thread.join();

  EXPECT_TRUE(header_written.load());
  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.debug, "pipe_response_payload_too_large");
}

TEST(ZenzNamedPipeClientTest, SpoofedPipeIsRejectedBeforeAnyPromptBytes) {
  const TestPipeName name = MakeTestPipeName();
  HANDLE server = CreateServerPipe(name.wide);
  ASSERT_NE(server, INVALID_HANDLE_VALUE);

  std::atomic<bool> received_prompt{false};
  std::thread server_thread([&] {
    if (ConnectServer(server)) {
      char byte = 0;
      DWORD bytes_read = 0;
      if (::ReadFile(server, &byte, 1, &bytes_read, nullptr) &&
          bytes_read > 0) {
        received_prompt = true;
      }
    }
    ::CloseHandle(server);
  });

  // This fake server runs in the test process, not mozc_zenz_scorer.exe.
  // The real verifier must reject it before WriteFile is called.
  ZenzNamedPipeClient client(500);
  const ZenzLiveResponse response = client.Convert(MakeRequest(name, 78));
  server_thread.join();

  // The client may close immediately after rejecting the peer. In that
  // case ConnectNamedPipe can return ERROR_NO_DATA even though CreateFileW
  // successfully connected; the peer rejection result is the invariant.
  EXPECT_FALSE(received_prompt.load());
  EXPECT_FALSE(response.ok);
  EXPECT_EQ(response.debug, "pipe_peer_untrusted");
}

TEST(ZenzNamedPipeClientTest, FirstPipeInstanceRejectsExistingPipeName) {
  const TestPipeName name = MakeTestPipeName();
  HANDLE first = CreateServerPipe(name.wide);
  ASSERT_NE(first, INVALID_HANDLE_VALUE);

  HANDLE second = ::CreateNamedPipeW(
      name.wide.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      1, 4096, 4096, 0, nullptr);
  EXPECT_EQ(second, INVALID_HANDLE_VALUE);
  if (second != INVALID_HANDLE_VALUE) {
    ::CloseHandle(second);
  }
  ::CloseHandle(first);
}

// Resolve a separately built test helper, not a server running in this test
// process. Bazel may place data dependencies in runfiles instead of the binary
// output directory, so check both locations.
std::wstring ExternalFakeServerHelperPath() {
  const wchar_t helper_name[] =
      L"zenz_named_pipe_fake_server_test_helper.exe";
  wchar_t module_path[32768] = {};
  const DWORD len = ::GetModuleFileNameW(nullptr, module_path, 32768);
  if (len > 0 && len < 32768) {
    const std::wstring current(module_path, len);
    const size_t separator = current.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
      const std::wstring sibling =
          current.substr(0, separator + 1) + helper_name;
      if (::GetFileAttributesW(sibling.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return sibling;
      }
    }
  }

  wchar_t runfiles[32768] = {};
  wchar_t workspace[1024] = {};
  const DWORD runfiles_len =
      ::GetEnvironmentVariableW(L"TEST_SRCDIR", runfiles, 32768);
  const DWORD workspace_len =
      ::GetEnvironmentVariableW(L"TEST_WORKSPACE", workspace, 1024);
  if (runfiles_len == 0 || runfiles_len >= 32768 ||
      workspace_len == 0 || workspace_len >= 1024) {
    return L"";
  }
  const std::wstring path = std::wstring(runfiles, runfiles_len) + L"\\" +
                            std::wstring(workspace, workspace_len) +
                            L"\\session\\" + helper_name;
  return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES
             ? path
             : L"";
}

TEST(ZenzNamedPipeClientTest, ExternalProcessSpoofRejectedBeforeAnyBytes) {
  const TestPipeName name = MakeTestPipeName();
  const std::wstring helper_path = ExternalFakeServerHelperPath();
  ASSERT_FALSE(helper_path.empty()) << "Separate fake pipe helper not found";

  std::wstring command_line =
      L"\"" + helper_path + L"\" \"" + name.wide + L"\"";
  std::vector<wchar_t> command_chars(command_line.begin(),
                                    command_line.end());
  command_chars.push_back(L'\0');
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  ASSERT_NE(::CreateProcessW(helper_path.c_str(), command_chars.data(),
                             nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &process), FALSE)
      << "Cannot start external fake server: " << ::GetLastError();
  ::CloseHandle(process.hThread);

  // No launch of the real scorer is needed: a random, test-specific pipe name
  // is provided explicitly. Wait for the fake process to claim the name.
  bool ready = false;
  for (int i = 0; i < 500; ++i) {
    if (::WaitNamedPipeW(name.wide.c_str(), 0)) {
      ready = true;
      break;
    }
    if (::WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
      break;
    }
    ::Sleep(10);
  }

  ZenzLiveResponse response;
  if (ready) {
    ZenzNamedPipeClient client(1000);  // Genuine production verifier.
    response = client.Convert(MakeRequest(name, 79));
  }

  const DWORD wait = ::WaitForSingleObject(process.hProcess, 5000);
  if (wait != WAIT_OBJECT_0) {
    ::TerminateProcess(process.hProcess, 99);
    ::WaitForSingleObject(process.hProcess, 5000);
  }
  DWORD exit_code = 0;
  const bool exit_code_ok =
      ::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
  ::CloseHandle(process.hProcess);

  ASSERT_TRUE(ready) << "External helper did not claim the test pipe";
  EXPECT_EQ(wait, WAIT_OBJECT_0) << "External helper did not finish";
  ASSERT_TRUE(exit_code_ok);
  EXPECT_EQ(exit_code, 0u) << "Fake server observed bytes or failed";
  EXPECT_FALSE(response.ok);
  EXPECT_EQ(response.debug, "pipe_peer_untrusted");
}

TEST(ZenzNamedPipeClientTest, OversizedPromptIsRejectedBeforePipeOpen) {
  ZenzLiveRequest request = MakeRequest(MakeTestPipeName(), 77);
  request.prompt.assign(8193, 'x');

  ZenzNamedPipeClient client(500);
  const ZenzLiveResponse response = client.Convert(request);

  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.debug, "prompt_too_large");
}

#endif  // _WIN32

#if !defined(_WIN32)
TEST(ZenzNamedPipeClientTest, SkippedOutsideWindows) {
  GTEST_SKIP() << "Windows named-pipe transport only";
}
#endif

}  // namespace
}  // namespace session
}  // namespace mozc
