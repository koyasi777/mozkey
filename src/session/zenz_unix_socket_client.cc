#include "session/zenz_unix_socket_client.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>

#include "absl/time/time.h"

#include "absl/time/clock.h"

#if defined(__APPLE__)
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern char **environ;
#endif

namespace mozc {
namespace session {

namespace {

constexpr char kDefaultNamedPipeEndpoint[] = "\\\\.\\pipe\\mozc_zenz_scorer";
constexpr char kDefaultUnixSocketEndpoint[] = "/tmp/mozc_zenz_scorer.sock";
constexpr char kDefaultScorerBinaryName[] = "mozc_zenz_scorer_macos";

constexpr uint32_t kZenzWireMagic = 0x315A4E5A;  // "ZNZ1"
constexpr uint16_t kZenzWireVersion = 1;
constexpr uint16_t kZenzWireKindRequest = 1;
constexpr uint16_t kZenzWireKindResponse = 2;

#pragma pack(push, 1)
struct ZenzWireRequestHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t kind;
  uint32_t generation;
  uint32_t timeout_msec;
  uint32_t max_output_chars;
  uint32_t prompt_size;
};

struct ZenzWireResponseHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t kind;
  uint32_t generation;
  uint32_t status;
  uint32_t latency_msec;
  uint32_t value_size;
  uint32_t debug_size;
};
#pragma pack(pop)

std::string ResolveSocketPath(const std::string& endpoint) {
  if (endpoint.empty() || endpoint == kDefaultNamedPipeEndpoint) {
    return kDefaultUnixSocketEndpoint;
  }
  return endpoint;
}

#if defined(__APPLE__)
std::string JoinPath(const std::string& dir, const std::string& file) {
  if (dir.empty()) {
    return file;
  }
  if (dir.back() == '/') {
    return dir + file;
  }
  return dir + "/" + file;
}

bool FileExists(const std::string& path) {
  struct stat st = {};
  return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string GetCurrentModuleDirectory() {
  uint32_t size = 0;
  if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) {
    return ".";
  }

  std::string path(size, '\0');
  if (_NSGetExecutablePath(path.data(), &size) != 0 || size == 0) {
    return ".";
  }
  if (!path.empty() && path.back() == '\0') {
    path.pop_back();
  }

  const size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return ".";
  }
  return path.substr(0, pos);
}

std::string ResolveBundledScorerPath(const std::string& module_dir) {
  const std::string adjacent_path =
      JoinPath(module_dir, kDefaultScorerBinaryName);
  if (FileExists(adjacent_path)) {
    return adjacent_path;
  }

  constexpr char kMacAppMacOSDir[] = "/Contents/MacOS";
  const size_t macos_dir_pos = module_dir.rfind(kMacAppMacOSDir);
  if (macos_dir_pos == std::string::npos) {
    return adjacent_path;
  }

  const std::string resources_dir =
      module_dir.substr(0, macos_dir_pos) + "/Contents/Resources";
  const std::string bundled_path =
      JoinPath(resources_dir, kDefaultScorerBinaryName);
  if (FileExists(bundled_path)) {
    return bundled_path;
  }

  constexpr char kNestedHelperResourcesDir[] = "/Contents/Resources/";
  const size_t nested_resources_pos =
      module_dir.rfind(kNestedHelperResourcesDir, macos_dir_pos);
  if (nested_resources_pos != std::string::npos) {
    const std::string outer_resources_dir =
        module_dir.substr(0, nested_resources_pos) + "/Contents/Resources";
    const std::string outer_bundled_path =
        JoinPath(outer_resources_dir, kDefaultScorerBinaryName);
    if (FileExists(outer_bundled_path)) {
      return outer_bundled_path;
    }
  }

  return adjacent_path;
}

std::string GetScorerPath() {
  const char* scorer_path = std::getenv("MOZC_ZENZ_SCORER");
  if (scorer_path != nullptr && *scorer_path != '\0') {
    return scorer_path;
  }
  return ResolveBundledScorerPath(GetCurrentModuleDirectory());
}

bool LaunchZenzScorerIfNeeded() {
  static std::atomic<int64_t> last_launch_millis{0};

  constexpr int64_t kLaunchThrottleMsec = 2000;

  const int64_t now = absl::ToUnixMillis(absl::Now());
  int64_t previous = last_launch_millis.load();
  if (previous != 0 && now - previous < kLaunchThrottleMsec) {
    return false;
  }

  while (!last_launch_millis.compare_exchange_weak(previous, now)) {
    if (previous != 0 && now - previous < kLaunchThrottleMsec) {
      return false;
    }
  }

  const std::string scorer_path = GetScorerPath();
  if (!FileExists(scorer_path)) {
    return false;
  }

  char* const argv[] = {
      const_cast<char*>(scorer_path.c_str()),
      nullptr,
  };

  posix_spawn_file_actions_t file_actions;
  if (posix_spawn_file_actions_init(&file_actions) != 0) {
    return false;
  }
  posix_spawn_file_actions_addopen(
      &file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(
      &file_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(
      &file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

  posix_spawnattr_t attr;
  if (posix_spawnattr_init(&attr) != 0) {
    posix_spawn_file_actions_destroy(&file_actions);
    return false;
  }

  pid_t pid = 0;
  const int spawn_result = posix_spawn(
      &pid, scorer_path.c_str(), &file_actions, &attr, argv, environ);

  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&file_actions);

  if (spawn_result != 0) {
    return false;
  }
  return true;
}

bool WriteAll(int fd, const void* data, size_t size) {
  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  size_t remaining = size;

  while (remaining > 0) {
    const ssize_t written = ::send(fd, ptr, remaining, 0);
    if (written <= 0) {
      return false;
    }
    ptr += written;
    remaining -= static_cast<size_t>(written);
  }

  return true;
}

bool ReadAll(int fd, void* data, size_t size) {
  uint8_t* ptr = static_cast<uint8_t*>(data);
  size_t remaining = size;

  while (remaining > 0) {
    const ssize_t read = ::recv(fd, ptr, remaining, 0);
    if (read <= 0) {
      return false;
    }
    ptr += read;
    remaining -= static_cast<size_t>(read);
  }

  return true;
}

bool SetSocketTimeout(int fd, uint32_t timeout_msec) {
  const uint32_t capped_timeout_msec = timeout_msec == 0 ? 1 : timeout_msec;
  timeval timeout = {};
  timeout.tv_sec = capped_timeout_msec / 1000;
  timeout.tv_usec = static_cast<suseconds_t>((capped_timeout_msec % 1000) * 1000);

  return ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
         ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
}

int ConnectWithAutoLaunch(const sockaddr_un& addr,
                          socklen_t addr_len,
                          uint32_t timeout_msec,
                          bool* launched) {
  *launched = false;

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  if (!SetSocketTimeout(fd, timeout_msec)) {
    ::close(fd);
    errno = ETIMEDOUT;
    return -1;
  }

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) == 0) {
    return fd;
  }

  const int first_errno = errno;
  ::close(fd);

  if (first_errno != ENOENT && first_errno != ECONNREFUSED) {
    errno = first_errno;
    return -1;
  }

  *launched = LaunchZenzScorerIfNeeded();

  constexpr uint32_t kColdStartRetryMsec = 50;
  constexpr uint32_t kMinAutoLaunchRetryBudgetMsec = 1000;
  const uint32_t retry_budget_msec = *launched
                                         ? std::max<uint32_t>(
                                               timeout_msec,
                                               kMinAutoLaunchRetryBudgetMsec)
                                         : std::max<uint32_t>(
                                               timeout_msec == 0 ? kColdStartRetryMsec
                                                                 : timeout_msec,
                                               kColdStartRetryMsec);
  const uint32_t retry_attempts =
      std::max<uint32_t>(1, retry_budget_msec / kColdStartRetryMsec);

  int last_retry_errno = first_errno;
  for (uint32_t attempt = 0; attempt < retry_attempts; ++attempt) {
    ::usleep(kColdStartRetryMsec * 1000);

    const int retry_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (retry_fd < 0) {
      continue;
    }
    if (!SetSocketTimeout(retry_fd, timeout_msec)) {
      ::close(retry_fd);
      continue;
    }
    if (::connect(retry_fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) == 0) {
      return retry_fd;
    }

    last_retry_errno = errno;
    ::close(retry_fd);
  }

  errno = last_retry_errno;
  return -1;
}
#endif

}  // namespace

bool ZenzUnixSocketClient::IsAvailable() const {
#if defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

ZenzLiveResponse ZenzUnixSocketClient::Convert(
    const ZenzLiveRequest& request) {
  ZenzLiveResponse response;
  response.generation = request.generation;
  response.key = request.key;
#if !defined(__APPLE__)
  response.ok = false;
  response.debug = "unix_socket_only_supported_on_macos";
  return response;
#else
  const absl::Time start = absl::Now();

  const std::string socket_path = ResolveSocketPath(request.pipe_name);
  if (socket_path.empty()) {
    response.ok = false;
    response.debug = "invalid_socket_path";
    return response;
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (socket_path.size() >= sizeof(addr.sun_path)) {
    response.ok = false;
    response.debug = "socket_path_too_long";
    return response;
  }
  std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

  const socklen_t addr_len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path.size() + 1);
  bool launched = false;
  const int fd = ConnectWithAutoLaunch(addr, addr_len, request.timeout_msec, &launched);
  if (fd < 0) {
    const int connect_errno = errno;
    response.ok = false;
    response.debug = (connect_errno == ETIMEDOUT)
                         ? "socket_timeout_setup_failed"
                         : ((connect_errno == ENOENT || connect_errno == ECONNREFUSED)
                                ? (launched ? "socket_connect_failed_after_launch"
                                            : "socket_connect_failed")
                                : "socket_connect_error");
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

  bool ok = WriteAll(fd, &request_header, sizeof(request_header));
  if (ok && !request.prompt.empty()) {
    ok = WriteAll(fd, request.prompt.data(), request.prompt.size());
  }
  if (!ok) {
    ::close(fd);
    response.ok = false;
    response.debug = "socket_write_failed";
    return response;
  }

  ZenzWireResponseHeader response_header = {};
  ok = ReadAll(fd, &response_header, sizeof(response_header));
  if (!ok) {
    ::close(fd);
    response.ok = false;
    response.debug = "socket_read_header_failed";
    return response;
  }

  if (response_header.magic != kZenzWireMagic ||
      response_header.version != kZenzWireVersion ||
      response_header.kind != kZenzWireKindResponse ||
      response_header.generation != request.generation) {
    ::close(fd);
    response.ok = false;
    response.debug = "socket_response_header_invalid";
    return response;
  }

  std::string value(response_header.value_size, '\0');
  if (response_header.value_size > 0) {
    ok = ReadAll(fd, value.data(), response_header.value_size);
  }

  std::string debug(response_header.debug_size, '\0');
  if (ok && response_header.debug_size > 0) {
    ok = ReadAll(fd, debug.data(), response_header.debug_size);
  }

  ::close(fd);

  if (!ok) {
    response.ok = false;
    response.debug = "socket_read_payload_failed";
    return response;
  }

  response.ok = response_header.status == 0;
  response.timeout = response_header.status == 2;
  response.value = std::move(value);
  response.debug = std::move(debug);
  response.latency = absl::Now() - start;
  return response;
#endif
}

}  // namespace session
}  // namespace mozc