#include "session/zenz_unix_socket_client.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "zenz/zenz_wire_protocol.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif  // !_WIN32

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

#if !defined(_WIN32)

constexpr uint32_t kMaxResponseValueBytes = 1024 * 1024;
constexpr uint32_t kMaxResponseDebugBytes = 64 * 1024;

class ScopedFd {
 public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;

  int get() const { return fd_; }

 private:
  int fd_;
};

bool IsValidSocketPath(const std::string& socket_path) {
  return !socket_path.empty() &&
         socket_path.find('\0') == std::string::npos &&
         socket_path.size() < sizeof(sockaddr_un::sun_path);
}

int RemainingTimeoutMsec(absl::Time deadline) {
  const absl::Duration remaining = deadline - absl::Now();
  if (remaining <= absl::ZeroDuration()) {
    return 0;
  }
  const int64_t remaining_msec =
      std::max<int64_t>(1, absl::ToInt64Milliseconds(remaining));
  return static_cast<int>(std::min<int64_t>(
      remaining_msec, std::numeric_limits<int>::max()));
}

bool WaitForFd(int fd, short events, absl::Time deadline, bool* timed_out) {
  while (true) {
    const int timeout_msec = RemainingTimeoutMsec(deadline);
    if (timeout_msec == 0) {
      *timed_out = true;
      return false;
    }

    pollfd descriptor = {};
    descriptor.fd = fd;
    descriptor.events = events;
    const int result = ::poll(&descriptor, 1, timeout_msec);
    if (result > 0) {
      return true;
    }
    if (result == 0) {
      *timed_out = true;
      return false;
    }
    if (errno != EINTR) {
      return false;
    }
  }
}

bool SetNonBlockingAndCloseOnExec(int fd) {
  const int descriptor_flags = ::fcntl(fd, F_GETFD, 0);
  if (descriptor_flags < 0 ||
      ::fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
    return false;
  }

  const int status_flags = ::fcntl(fd, F_GETFL, 0);
  return status_flags >= 0 &&
         ::fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) == 0;
}

bool DisableSigPipe(int fd) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                      sizeof(enabled)) == 0;
#else
  return true;
#endif
}

bool ConnectWithDeadline(int fd, const std::string& socket_path,
                         absl::Time deadline, bool* timed_out) {
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  const socklen_t address_length = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path.size() + 1);
#if defined(__APPLE__)
  address.sun_len = static_cast<uint8_t>(address_length);
#endif

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address),
                address_length) == 0) {
    return true;
  }
  if (errno != EINPROGRESS && errno != EAGAIN &&
      errno != EWOULDBLOCK) {
    return false;
  }
  if (!WaitForFd(fd, POLLOUT, deadline, timed_out)) {
    return false;
  }

  int socket_error = 0;
  socklen_t socket_error_size = sizeof(socket_error);
  return ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                      &socket_error_size) == 0 &&
         socket_error == 0;
}

int SendFlags() {
#if defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

bool WriteAll(int fd, const void* data, uint32_t size, absl::Time deadline,
              bool* timed_out) {
  const uint8_t* current = static_cast<const uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    const ssize_t written =
        ::send(fd, current, remaining, SendFlags());
    if (written > 0) {
      current += written;
      remaining -= static_cast<uint32_t>(written);
      continue;
    }
    if (written == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (!WaitForFd(fd, POLLOUT, deadline, timed_out)) {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

bool ReadAll(int fd, void* data, uint32_t size, absl::Time deadline,
             bool* timed_out) {
  uint8_t* current = static_cast<uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    const ssize_t read_size = ::recv(fd, current, remaining, 0);
    if (read_size > 0) {
      current += read_size;
      remaining -= static_cast<uint32_t>(read_size);
      continue;
    }
    if (read_size == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (!WaitForFd(fd, POLLIN, deadline, timed_out)) {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

#endif  // !_WIN32

}  // namespace

ZenzUnixSocketClient::ZenzUnixSocketClient(std::string socket_path)
    : socket_path_(std::move(socket_path)) {}

bool ZenzUnixSocketClient::IsAvailable() const {
#if defined(_WIN32)
  return false;
#else
  return IsValidSocketPath(socket_path_);
#endif
}

ZenzLiveResponse ZenzUnixSocketClient::Convert(
    const ZenzLiveRequest& request) {
  ZenzLiveResponse response;
  response.generation = request.generation;
  response.key = request.key;

#if defined(_WIN32)
  response.debug = "unix_socket_not_supported_on_windows";
  return response;
#else
  if (!IsValidSocketPath(socket_path_)) {
    response.debug = "invalid_socket_path";
    return response;
  }
  if (request.prompt.size() > std::numeric_limits<uint32_t>::max()) {
    response.debug = "prompt_too_large";
    return response;
  }

  const absl::Time start = absl::Now();
  const absl::Time deadline =
      start + absl::Milliseconds(std::max<uint32_t>(1, request.timeout_msec));

  ScopedFd socket_fd(::socket(AF_UNIX, SOCK_STREAM, 0));
  if (socket_fd.get() < 0) {
    response.debug = "socket_create_failed";
    return response;
  }
  if (!SetNonBlockingAndCloseOnExec(socket_fd.get()) ||
      !DisableSigPipe(socket_fd.get())) {
    response.debug = "socket_configure_failed";
    return response;
  }

  bool timed_out = false;
  if (!ConnectWithDeadline(socket_fd.get(), socket_path_, deadline,
                           &timed_out)) {
    response.timeout = timed_out;
    response.debug =
        timed_out ? "socket_connect_timeout" : "socket_connect_failed";
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

  bool ok = WriteAll(socket_fd.get(), &request_header,
                     sizeof(request_header), deadline, &timed_out);
  if (ok && !request.prompt.empty()) {
    ok = WriteAll(socket_fd.get(), request.prompt.data(),
                  static_cast<uint32_t>(request.prompt.size()), deadline,
                  &timed_out);
  }
  if (!ok) {
    response.timeout = timed_out;
    response.debug =
        timed_out ? "socket_write_timeout" : "socket_write_failed";
    return response;
  }

  ZenzWireResponseHeader response_header = {};
  ok = ReadAll(socket_fd.get(), &response_header, sizeof(response_header),
               deadline, &timed_out);
  if (!ok) {
    response.timeout = timed_out;
    response.debug = timed_out ? "socket_read_header_timeout"
                               : "socket_read_header_failed";
    return response;
  }
  if (response_header.magic != kZenzWireMagic ||
      response_header.version != kZenzWireVersion ||
      response_header.kind != kZenzWireKindResponse ||
      response_header.generation != request.generation) {
    response.debug = "socket_response_header_invalid";
    return response;
  }
  if (response_header.value_size > kMaxResponseValueBytes ||
      response_header.debug_size > kMaxResponseDebugBytes) {
    response.debug = "socket_response_payload_too_large";
    return response;
  }

  std::string value(response_header.value_size, '\0');
  if (response_header.value_size > 0) {
    ok = ReadAll(socket_fd.get(), value.data(), response_header.value_size,
                 deadline, &timed_out);
  }
  std::string debug(response_header.debug_size, '\0');
  if (ok && response_header.debug_size > 0) {
    ok = ReadAll(socket_fd.get(), debug.data(), response_header.debug_size,
                 deadline, &timed_out);
  }
  if (!ok) {
    response.timeout = timed_out;
    response.debug =
        timed_out ? "socket_read_payload_timeout" : "socket_read_payload_failed";
    return response;
  }

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
