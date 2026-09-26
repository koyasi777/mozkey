#include "session/zenz_named_pipe_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "zenz/zenz_wire_protocol.h"

#if defined(_WIN32)
#include <windows.h>
#endif  // _WIN32

namespace mozc {
namespace session {
namespace {

using ::mozc::zenz::kZenzWireKindRequest;
using ::mozc::zenz::kZenzWireKindResponse;
using ::mozc::zenz::kZenzWireMagic;
using ::mozc::zenz::kZenzWireStatusOk;
using ::mozc::zenz::kZenzWireStatusTimeout;
using ::mozc::zenz::kZenzWireVersion;
using ::mozc::zenz::ZenzWireRequestHeader;
using ::mozc::zenz::ZenzWireResponseHeader;

#if defined(_WIN32)

constexpr uint32_t kMaxPromptBytes = 8192;
constexpr uint32_t kMaxResponseValueBytes = 1024 * 1024;
constexpr uint32_t kMaxResponseDebugBytes = 64 * 1024;


using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

enum class PipeIoResult {
  kOk,
  kTimeout,
  kError,
};

void ZenzPipeDebugOutput(const std::wstring& message);

class ScopedWinHandle {
 public:
  explicit ScopedWinHandle(HANDLE handle = nullptr) : handle_(handle) {}
  ~ScopedWinHandle() { Reset(); }

  ScopedWinHandle(const ScopedWinHandle&) = delete;
  ScopedWinHandle& operator=(const ScopedWinHandle&) = delete;

  HANDLE get() const { return handle_; }

  void Reset(HANDLE handle = nullptr) {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      ::CloseHandle(handle_);
    }
    handle_ = handle;
  }

 private:
  HANDLE handle_;
};

DWORD RemainingMsec(Deadline deadline) {
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - Clock::now());
  if (remaining.count() <= 0) {
    return 0;
  }
  return static_cast<DWORD>(std::min<int64_t>(
      std::max<int64_t>(1, remaining.count()),
      std::numeric_limits<DWORD>::max()));
}

bool SleepWithinDeadline(DWORD requested_msec, Deadline deadline,
                         HANDLE cancellation_event) {
  const DWORD remaining = RemainingMsec(deadline);
  if (remaining == 0) {
    return false;
  }
  return ::WaitForSingleObject(cancellation_event,
                               std::min(requested_msec, remaining)) ==
             WAIT_TIMEOUT &&
         RemainingMsec(deadline) > 0;
}

void CancelAndReap(HANDLE handle, OVERLAPPED* overlapped) {
  if (!::CancelIoEx(handle, overlapped)) {
    const DWORD error = ::GetLastError();
    if (error != ERROR_NOT_FOUND) {
      ZenzPipeDebugOutput(
          std::wstring(L"CancelIoEx failed error=")
              .append(std::to_wstring(error)));
    }
  }

  DWORD ignored = 0;
  if (!::GetOverlappedResult(handle, overlapped, &ignored, TRUE)) {
    const DWORD error = ::GetLastError();
    if (error != ERROR_OPERATION_ABORTED) {
      ZenzPipeDebugOutput(
          std::wstring(L"GetOverlappedResult after cancel failed error=")
              .append(std::to_wstring(error)));
    }
  }
}

PipeIoResult WaitForPendingIo(HANDLE handle, OVERLAPPED* overlapped,
                              Deadline deadline, DWORD* transferred,
                              HANDLE cancellation_event) {
  const DWORD remaining = RemainingMsec(deadline);
  if (remaining == 0) {
    CancelAndReap(handle, overlapped);
    return PipeIoResult::kTimeout;
  }
  const HANDLE events[] = {cancellation_event, overlapped->hEvent};
  const DWORD wait = ::WaitForMultipleObjects(2, events, FALSE, remaining);
  if (wait == WAIT_OBJECT_0 + 1) {
    return ::GetOverlappedResult(handle, overlapped, transferred, FALSE)
               ? PipeIoResult::kOk
               : PipeIoResult::kError;
  }
  CancelAndReap(handle, overlapped);
  return wait == WAIT_TIMEOUT ? PipeIoResult::kTimeout : PipeIoResult::kError;
}

PipeIoResult WriteSomeUntil(HANDLE handle, const void* data, DWORD size,
                            Deadline deadline, DWORD* transferred,
                            HANDLE cancellation_event) {
  if (::WaitForSingleObject(cancellation_event, 0) == WAIT_OBJECT_0) {
    return PipeIoResult::kError;
  }
  ScopedWinHandle event(
      ::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (event.get() == nullptr) {
    return PipeIoResult::kError;
  }

  OVERLAPPED overlapped = {};
  overlapped.hEvent = event.get();

  if (::WriteFile(handle, data, size, nullptr, &overlapped)) {
    return ::GetOverlappedResult(
               handle, &overlapped, transferred, FALSE)
               ? PipeIoResult::kOk
               : PipeIoResult::kError;
  }

  if (::GetLastError() != ERROR_IO_PENDING) {
    return PipeIoResult::kError;
  }
  return WaitForPendingIo(handle, &overlapped, deadline, transferred,
                          cancellation_event);
}

PipeIoResult ReadSomeUntil(HANDLE handle, void* data, DWORD size,
                           Deadline deadline, DWORD* transferred,
                           HANDLE cancellation_event) {
  if (::WaitForSingleObject(cancellation_event, 0) == WAIT_OBJECT_0) {
    return PipeIoResult::kError;
  }
  ScopedWinHandle event(
      ::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (event.get() == nullptr) {
    return PipeIoResult::kError;
  }

  OVERLAPPED overlapped = {};
  overlapped.hEvent = event.get();

  if (::ReadFile(handle, data, size, nullptr, &overlapped)) {
    return ::GetOverlappedResult(
               handle, &overlapped, transferred, FALSE)
               ? PipeIoResult::kOk
               : PipeIoResult::kError;
  }

  if (::GetLastError() != ERROR_IO_PENDING) {
    return PipeIoResult::kError;
  }
  return WaitForPendingIo(handle, &overlapped, deadline, transferred,
                          cancellation_event);
}

PipeIoResult WriteAllUntil(HANDLE handle, const void* data, uint32_t size,
                           Deadline deadline, HANDLE cancellation_event) {
  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    DWORD written = 0;
    const PipeIoResult result = WriteSomeUntil(handle, ptr, remaining, deadline,
                                               &written, cancellation_event);
    if (result != PipeIoResult::kOk) {
      return result;
    }
    if (written == 0) {
      return PipeIoResult::kError;
    }
    ptr += written;
    remaining -= written;
  }
  return PipeIoResult::kOk;
}

PipeIoResult ReadAllUntil(HANDLE handle, void* data, uint32_t size,
                          Deadline deadline, HANDLE cancellation_event) {
  uint8_t* ptr = static_cast<uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    DWORD read = 0;
    const PipeIoResult result = ReadSomeUntil(handle, ptr, remaining, deadline,
                                              &read, cancellation_event);
    if (result != PipeIoResult::kOk) {
      return result;
    }
    if (read == 0) {
      return PipeIoResult::kError;
    }
    ptr += read;
    remaining -= read;
  }
  return PipeIoResult::kOk;
}

std::wstring Utf8ToWidePipeName(const std::string& s) {
  if (s.empty()) {
    return L"";
  }

  const int size = ::MultiByteToWideChar(
      CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (size <= 0) {
    return L"";
  }

  std::wstring output(size, L'\0');
  ::MultiByteToWideChar(
      CP_UTF8, 0, s.data(), static_cast<int>(s.size()), output.data(), size);
  return output;
}

void ZenzPipeDebugOutput(const std::wstring& message) {
  std::wstring line = L"[zenz-pipe] ";
  line.append(message);
  line.push_back(L'\n');
  ::OutputDebugStringW(line.c_str());
}

std::wstring RedactedStatsWide(const wchar_t* label, size_t bytes) {
  std::wstring output(label);
  output.append(L"_bytes=");
  output.append(std::to_wstring(bytes));
  return output;
}

HANDLE TryOpenPipeOnce(const std::wstring& pipe_name) {
  return ::CreateFileW(
      pipe_name.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
      nullptr);
}

std::wstring GetCurrentModuleDirectory() {
  wchar_t path[MAX_PATH] = {};
  const DWORD size = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (size == 0 || size >= MAX_PATH) {
    return L".";
  }

  std::wstring full(path, size);
  const size_t pos = full.find_last_of(L"\\/");
  if (pos == std::wstring::npos) {
    return L".";
  }

  return full.substr(0, pos);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& file) {
  if (dir.empty()) {
    return file;
  }
  if (dir.back() == L'\\' || dir.back() == L'/') {
    return dir + file;
  }
  return dir + L"\\" + file;
}

bool LaunchZenzScorerIfNeeded() {
  // Do not make scorer launch a one-shot decision.  The scorer process may be
  // killed independently from mozc_server.exe during development, upgrade, or
  // crash recovery.  Throttle only very recent launch attempts to avoid spawning
  // many scorer processes while the pipe is still being created.
  static std::atomic<DWORD> last_launch_tick{0};

  constexpr DWORD kLaunchThrottleMsec = 2000;

  const DWORD now = ::GetTickCount();
  DWORD previous = last_launch_tick.load();

  if (previous != 0 && now - previous < kLaunchThrottleMsec) {
    ZenzPipeDebugOutput(
        std::wstring(L"scorer launch throttled elapsed_msec=")
            .append(std::to_wstring(now - previous)));
    return false;
  }

  while (!last_launch_tick.compare_exchange_weak(previous, now)) {
    if (previous != 0 && now - previous < kLaunchThrottleMsec) {
      ZenzPipeDebugOutput(
          std::wstring(L"scorer launch throttled elapsed_msec=")
              .append(std::to_wstring(now - previous)));
      return false;
    }
  }

  const std::wstring dir = GetCurrentModuleDirectory();
  const std::wstring scorer_path = JoinPath(dir, L"mozc_zenz_scorer.exe");

  const DWORD attr = ::GetFileAttributesW(scorer_path.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES ||
      (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    ZenzPipeDebugOutput(L"scorer not found");
    return false;
  }

  std::wstring command_line = L"\"";
  command_line.append(scorer_path);
  command_line.append(L"\"");

  std::vector<wchar_t> command_line_buffer(
      command_line.begin(), command_line.end());
  command_line_buffer.push_back(L'\0');

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION process = {};

  ZenzPipeDebugOutput(L"launch scorer");

  if (!::CreateProcessW(
          nullptr,
          command_line_buffer.data(),
          nullptr,
          nullptr,
          FALSE,
          CREATE_NO_WINDOW,
          nullptr,
          dir.c_str(),
          &startup,
          &process)) {
    const DWORD error = ::GetLastError();
    ZenzPipeDebugOutput(
        std::wstring(L"launch scorer failed error=")
            .append(std::to_wstring(error)));
    return false;
  }

  ::CloseHandle(process.hThread);
  ::CloseHandle(process.hProcess);
  return true;
}

HANDLE OpenPipeWithAutoLaunch(const std::wstring& pipe_name,
                              uint32_t request_timeout_msec,
                              Deadline transport_deadline,
                              HANDLE cancellation_event) {
  if (::WaitForSingleObject(cancellation_event, 0) == WAIT_OBJECT_0) {
    return INVALID_HANDLE_VALUE;
  }
  ZenzPipeDebugOutput(
      std::wstring(L"CreateFileW begin ")
          .append(RedactedStatsWide(
              L"pipe_name", pipe_name.size() * sizeof(wchar_t))));

  HANDLE pipe = TryOpenPipeOnce(pipe_name);
  if (pipe != INVALID_HANDLE_VALUE) {
    ZenzPipeDebugOutput(L"CreateFileW succeeded");
    return pipe;
  }

  const DWORD error = ::GetLastError();
  ZenzPipeDebugOutput(
      std::wstring(L"CreateFileW failed error=")
          .append(std::to_wstring(error)));

  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
    const bool launched = LaunchZenzScorerIfNeeded();
    ZenzPipeDebugOutput(
        std::wstring(L"scorer launch requested launched=")
            .append(launched ? L"true" : L"false"));

    constexpr DWORD kColdStartRetryMsec = 50;
    const uint32_t retry_budget_msec =
        std::max<uint32_t>(request_timeout_msec, kColdStartRetryMsec);
    const Deadline retry_deadline = std::min(
        transport_deadline,
        Clock::now() + std::chrono::milliseconds(retry_budget_msec));

    int attempt = 0;
    while (::WaitForSingleObject(cancellation_event, 0) != WAIT_OBJECT_0 &&
           RemainingMsec(retry_deadline) > 0) {
      ++attempt;
      if (!SleepWithinDeadline(kColdStartRetryMsec, retry_deadline,
                               cancellation_event)) {
        break;
      }

      pipe = TryOpenPipeOnce(pipe_name);
      if (pipe != INVALID_HANDLE_VALUE) {
        ZenzPipeDebugOutput(
            std::wstring(L"CreateFileW succeeded after cold start retry attempt=")
                .append(std::to_wstring(attempt)));
        return pipe;
      }

      const DWORD retry_error = ::GetLastError();
      if (retry_error == ERROR_PIPE_BUSY) {
        const DWORD wait_msec =
            std::min<DWORD>(kColdStartRetryMsec,
                            RemainingMsec(retry_deadline));
        if (wait_msec > 0) {
          ::WaitNamedPipeW(pipe_name.c_str(), wait_msec);
        }
        pipe = TryOpenPipeOnce(pipe_name);
        if (pipe != INVALID_HANDLE_VALUE) {
          ZenzPipeDebugOutput(
              std::wstring(
                  L"CreateFileW succeeded after cold start busy retry attempt=")
                  .append(std::to_wstring(attempt)));
          return pipe;
        }
      }

      if (attempt == 1 || RemainingMsec(retry_deadline) == 0) {
        ZenzPipeDebugOutput(
            std::wstring(L"CreateFileW cold start retry failed attempt=")
                .append(std::to_wstring(attempt))
                .append(L" error=")
                .append(std::to_wstring(retry_error)));
      }
    }

    ZenzPipeDebugOutput(
        std::wstring(L"CreateFileW cold start retry exhausted budget_msec=")
            .append(std::to_wstring(retry_budget_msec)));
    return INVALID_HANDLE_VALUE;
  }

  if (error == ERROR_PIPE_BUSY) {
    constexpr int kBusyRetryAttempts = 3;
    constexpr DWORD kBusyRetryMsec = 20;

    for (int attempt = 0; attempt < kBusyRetryAttempts; ++attempt) {
      const DWORD wait_msec =
          std::min<DWORD>(kBusyRetryMsec,
                          RemainingMsec(transport_deadline));
      if (wait_msec == 0 ||
          ::WaitForSingleObject(cancellation_event, 0) == WAIT_OBJECT_0) {
        break;
      }
      ::WaitNamedPipeW(pipe_name.c_str(), wait_msec);

      ZenzPipeDebugOutput(
          std::wstring(L"CreateFileW retry attempt=")
              .append(std::to_wstring(attempt + 1)));

      pipe = TryOpenPipeOnce(pipe_name);
      if (pipe != INVALID_HANDLE_VALUE) {
        ZenzPipeDebugOutput(L"CreateFileW succeeded after retry");
        return pipe;
      }
    }
  }

  return INVALID_HANDLE_VALUE;
}

#endif  // _WIN32

}  // namespace

ZenzNamedPipeClient::ZenzNamedPipeClient(
    uint32_t transport_safety_timeout_msec)
    : transport_safety_timeout_msec_(
          std::max<uint32_t>(1, transport_safety_timeout_msec)) {}

bool ZenzNamedPipeClient::IsAvailable() const {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

ZenzLiveResponse ZenzNamedPipeClient::Convert(
    const ZenzLiveRequest& request) {
  ZenzLiveResponse response;
  response.generation = request.generation;
  response.key = request.key;

#if defined(_WIN32)
  ZenzPipeDebugOutput(
      std::wstring(L"Convert entered generation=")
          .append(std::to_wstring(request.generation))
          .append(L" ")
          .append(RedactedStatsWide(L"key", request.key.size()))
          .append(L" ")
          .append(RedactedStatsWide(L"prompt", request.prompt.size()))
          .append(L" timeout_msec=")
          .append(std::to_wstring(request.timeout_msec)));
#endif

#if !defined(_WIN32)
  response.ok = false;
  response.debug = "named_pipe_only_supported_on_windows";
  return response;
#else
  ScopedWinHandle cancellation_event(
      ::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (cancellation_event.get() == nullptr) {
    response.debug = "pipe_cancellation_event_failed";
    return response;
  }
  ScopedInterrupt interrupt(*this,
                            [&] { ::SetEvent(cancellation_event.get()); });

  const absl::Time start = absl::Now();
  const Deadline transport_deadline =
      Clock::now() +
      std::chrono::milliseconds(transport_safety_timeout_msec_);

  if (request.prompt.size() > kMaxPromptBytes) {
    response.ok = false;
    response.debug = "prompt_too_large";
    return response;
  }

  const std::wstring pipe_name = Utf8ToWidePipeName(request.pipe_name);
  if (pipe_name.empty()) {
    response.ok = false;
    response.debug = "invalid_pipe_name";
    return response;
  }

  ScopedWinHandle pipe(OpenPipeWithAutoLaunch(pipe_name, request.timeout_msec,
                                              transport_deadline,
                                              cancellation_event.get()));

  if (pipe.get() == INVALID_HANDLE_VALUE) {
    response.ok = false;
    response.timeout = RemainingMsec(transport_deadline) == 0;
    response.debug =
        response.timeout ? "pipe_open_timeout" : "pipe_open_failed";
    return response;
  }

  ZenzWireRequestHeader request_header = {};
  request_header.magic = kZenzWireMagic;
  request_header.version = kZenzWireVersion;
  request_header.kind = kZenzWireKindRequest;
  request_header.generation = request.generation;
  request_header.timeout_msec = request.timeout_msec;
  request_header.max_output_chars = request.max_output_chars;
  request_header.prompt_size = static_cast<uint32_t>(request.prompt.size());

  PipeIoResult io =
      WriteAllUntil(pipe.get(), &request_header, sizeof(request_header),
                    transport_deadline, cancellation_event.get());
  if (io == PipeIoResult::kOk && !request.prompt.empty()) {
    io = WriteAllUntil(pipe.get(), request.prompt.data(),
                       static_cast<uint32_t>(request.prompt.size()),
                       transport_deadline, cancellation_event.get());
  }

  if (io != PipeIoResult::kOk) {
    response.ok = false;
    response.timeout = io == PipeIoResult::kTimeout;
    response.debug =
        response.timeout ? "pipe_write_timeout" : "pipe_write_failed";
    return response;
  }

  ZenzWireResponseHeader response_header = {};
  io = ReadAllUntil(pipe.get(), &response_header, sizeof(response_header),
                    transport_deadline, cancellation_event.get());
  if (io != PipeIoResult::kOk) {
    response.ok = false;
    response.timeout = io == PipeIoResult::kTimeout;
    response.debug =
        response.timeout ? "pipe_read_header_timeout"
                         : "pipe_read_header_failed";
    return response;
  }

  if (response_header.magic != kZenzWireMagic ||
      response_header.version != kZenzWireVersion ||
      response_header.kind != kZenzWireKindResponse ||
      response_header.generation != request.generation) {
    response.ok = false;
    response.debug = "pipe_response_header_invalid";
    return response;
  }
  if (response_header.value_size > kMaxResponseValueBytes ||
      response_header.debug_size > kMaxResponseDebugBytes) {
    response.ok = false;
    response.debug = "pipe_response_payload_too_large";
    return response;
  }

  std::string value(response_header.value_size, '\0');
  if (response_header.value_size > 0) {
    io = ReadAllUntil(pipe.get(), value.data(), response_header.value_size,
                      transport_deadline, cancellation_event.get());
  }

  std::string debug(response_header.debug_size, '\0');
  if (io == PipeIoResult::kOk && response_header.debug_size > 0) {
    io = ReadAllUntil(pipe.get(), debug.data(), response_header.debug_size,
                      transport_deadline, cancellation_event.get());
  }

  if (io != PipeIoResult::kOk) {
    response.ok = false;
    response.timeout = io == PipeIoResult::kTimeout;
    response.debug =
        response.timeout ? "pipe_read_payload_timeout"
                         : "pipe_read_payload_failed";
    return response;
  }

#if defined(_WIN32)
  ZenzPipeDebugOutput(
      std::wstring(L"Convert response status=")
          .append(std::to_wstring(response_header.status))
          .append(L" latency_msec=")
          .append(std::to_wstring(response_header.latency_msec))
          .append(L" ")
          .append(RedactedStatsWide(L"value", value.size()))
          .append(L" ")
          .append(RedactedStatsWide(L"debug", debug.size())));
#endif

  response.ok = response_header.status == kZenzWireStatusOk;
  response.timeout = response_header.status == kZenzWireStatusTimeout;
  response.value = std::move(value);
  response.debug = std::move(debug);
  response.latency = absl::Now() - start;
  return response;
#endif  // _WIN32
}

}  // namespace session
}  // namespace mozc
