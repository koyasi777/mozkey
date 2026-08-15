#include "zenz_scorer/posix_server.h"

#if defined(_WIN32)
#error "posix_server.cc must not be built on Windows"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "zenz/zenz_unix_socket_path.h"
#include "zenz/zenz_wire_protocol.h"

namespace mozc {
namespace zenz_scorer {
namespace {

using ::mozc::zenz::kZenzWireKindRequest;
using ::mozc::zenz::kZenzWireKindResponse;
using ::mozc::zenz::kZenzWireMagic;
using ::mozc::zenz::kZenzWireStatusError;
using ::mozc::zenz::kZenzWireStatusOk;
using ::mozc::zenz::kZenzWireStatusTimeout;
using ::mozc::zenz::kZenzWireVersion;
using ::mozc::zenz::ZenzWireRequestHeader;
using ::mozc::zenz::ZenzWireResponseHeader;

constexpr uint32_t kMaxPromptBytes = 8192;
constexpr uint32_t kMaxOutputChars = 256;
// Production live-conversion requests normally carry their own much shorter
// deadline. This is only the defensive upper ceiling accepted by the local
// scorer, and must also permit bounded package/runtime verification on slower
// supported hosts.
constexpr uint32_t kMaxRequestTimeoutMsec = 30000;
constexpr uint32_t kMaxResponseValueBytes = 1024 * 1024;
constexpr uint32_t kMaxResponseDebugBytes = 64 * 1024;
constexpr int kHeaderReadTimeoutMsec = 5000;
constexpr int kResponseWriteTimeoutMsec = 1000;

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

std::string ErrnoTag(const char* prefix) {
  return std::string(prefix) + "_errno_" + std::to_string(errno);
}

bool SetCloseOnExec(int fd) {
  const int flags = ::fcntl(fd, F_GETFD, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool DisableSigPipe(int fd) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                      sizeof(enabled)) == 0;
#else
  static_cast<void>(fd);
  return true;
#endif
}

int RemainingMsec(Deadline deadline) {
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                            Clock::now());
  if (remaining.count() <= 0) {
    return 0;
  }
  return static_cast<int>(std::min<int64_t>(
      std::max<int64_t>(1, remaining.count()),
      std::numeric_limits<int>::max()));
}

bool WaitForFd(int fd, short events, Deadline deadline, bool* timed_out) {
  while (true) {
    const int timeout_msec = RemainingMsec(deadline);
    if (timeout_msec == 0) {
      *timed_out = true;
      return false;
    }

    pollfd descriptor = {};
    descriptor.fd = fd;
    descriptor.events = events;
    const int result = ::poll(&descriptor, 1, timeout_msec);
    if (result > 0) {
      if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
          (descriptor.revents & events) == 0) {
        return false;
      }
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

bool ReadAll(int fd, void* data, uint32_t size, Deadline deadline,
             bool* timed_out) {
  uint8_t* current = static_cast<uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    if (!WaitForFd(fd, POLLIN, deadline, timed_out)) {
      return false;
    }
    const ssize_t read_size = ::recv(fd, current, remaining, 0);
    if (read_size > 0) {
      current += read_size;
      remaining -= static_cast<uint32_t>(read_size);
      continue;
    }
    if (read_size == 0) {
      return false;
    }
    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      return false;
    }
  }
  return true;
}

int SendFlags() {
#if defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

bool WriteAll(int fd, const void* data, uint32_t size, Deadline deadline,
              bool* timed_out) {
  const uint8_t* current = static_cast<const uint8_t*>(data);
  uint32_t remaining = size;
  while (remaining > 0) {
    if (!WaitForFd(fd, POLLOUT, deadline, timed_out)) {
      return false;
    }
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
    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      return false;
    }
  }
  return true;
}

bool EnsurePrivateDirectory(const std::string& directory,
                            std::string* error) {
  struct stat status = {};
  if (::lstat(directory.c_str(), &status) != 0) {
    if (errno != ENOENT) {
      *error = ErrnoTag("directory_lstat_failed");
      return false;
    }
    if (::mkdir(directory.c_str(), 0700) != 0) {
      *error = ErrnoTag("directory_mkdir_failed");
      return false;
    }
    if (::lstat(directory.c_str(), &status) != 0) {
      *error = ErrnoTag("directory_post_mkdir_lstat_failed");
      return false;
    }
  }

  if (!S_ISDIR(status.st_mode)) {
    *error = "directory_not_directory";
    return false;
  }
  if (status.st_uid != ::geteuid()) {
    *error = "directory_wrong_owner";
    return false;
  }
  if (::chmod(directory.c_str(), 0700) != 0) {
    *error = ErrnoTag("directory_chmod_failed");
    return false;
  }
  return true;
}

int OpenPrivateLockFile(const std::string& lock_path, std::string* error) {
  int flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const int fd = ::open(lock_path.c_str(), flags, 0600);
  if (fd < 0) {
    *error = ErrnoTag("lock_open_failed");
    return -1;
  }
  if (!SetCloseOnExec(fd)) {
    *error = ErrnoTag("lock_cloexec_failed");
    ::close(fd);
    return -1;
  }

  struct stat status = {};
  if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_uid != ::geteuid()) {
    *error = "lock_invalid";
    ::close(fd);
    return -1;
  }
  if (::fchmod(fd, 0600) != 0) {
    *error = ErrnoTag("lock_chmod_failed");
    ::close(fd);
    return -1;
  }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    *error = errno == EWOULDBLOCK ? "lock_busy"
                                 : ErrnoTag("lock_flock_failed");
    ::close(fd);
    return -1;
  }
  return fd;
}

bool RemoveOwnedSocket(const std::string& socket_path, bool require_socket,
                       std::string* error) {
  struct stat status = {};
  if (::lstat(socket_path.c_str(), &status) != 0) {
    if (errno == ENOENT) {
      return true;
    }
    *error = ErrnoTag("socket_lstat_failed");
    return false;
  }
  if (status.st_uid != ::geteuid()) {
    *error = "socket_wrong_owner";
    return false;
  }
  if (require_socket && !S_ISSOCK(status.st_mode)) {
    *error = "socket_path_not_socket";
    return false;
  }
  if (::unlink(socket_path.c_str()) != 0) {
    *error = ErrnoTag("socket_unlink_failed");
    return false;
  }
  return true;
}

bool SendResponse(int fd, uint32_t generation,
                  const PosixZenzResponse& response) {
  ZenzWireResponseHeader header = {};
  header.magic = kZenzWireMagic;
  header.version = kZenzWireVersion;
  header.kind = kZenzWireKindResponse;
  header.generation = generation;
  header.status = response.status;
  header.latency_msec = response.latency_msec;
  header.value_size = static_cast<uint32_t>(response.value.size());
  header.debug_size = static_cast<uint32_t>(response.debug.size());

  const Deadline deadline =
      Clock::now() + std::chrono::milliseconds(kResponseWriteTimeoutMsec);
  bool timed_out = false;
  if (!WriteAll(fd, &header, sizeof(header), deadline, &timed_out)) {
    return false;
  }
  if (!response.value.empty() &&
      !WriteAll(fd, response.value.data(), header.value_size, deadline,
                &timed_out)) {
    return false;
  }
  return response.debug.empty() ||
         WriteAll(fd, response.debug.data(), header.debug_size, deadline,
                  &timed_out);
}

PosixZenzResponse ErrorResponse(const std::string& debug) {
  PosixZenzResponse response;
  response.status = kZenzWireStatusError;
  response.debug = debug;
  return response;
}

}  // namespace

PosixZenzScorerServer::PosixZenzScorerServer(
    std::string socket_path, PosixZenzBackend backend)
    : socket_path_(std::move(socket_path)), backend_(std::move(backend)) {}

PosixZenzScorerServer::~PosixZenzScorerServer() { Reset(); }

bool PosixZenzScorerServer::Start(std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  Reset();

  if (!::mozc::zenz::IsValidZenzUnixSocketPath(socket_path_)) {
    *error = "invalid_socket_path";
    return false;
  }
  const size_t slash = socket_path_.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    *error = "invalid_socket_directory";
    return false;
  }
  directory_path_ = socket_path_.substr(0, slash);
  lock_path_ = directory_path_ + "/scorer.lock";

  if (!EnsurePrivateDirectory(directory_path_, error)) {
    return false;
  }
  lock_fd_ = OpenPrivateLockFile(lock_path_, error);
  if (lock_fd_ < 0) {
    return false;
  }
  if (!RemoveOwnedSocket(socket_path_, true, error)) {
    Reset();
    return false;
  }

  listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd_ < 0 || !SetCloseOnExec(listen_fd_) ||
      !DisableSigPipe(listen_fd_)) {
    *error = listen_fd_ < 0 ? ErrnoTag("socket_create_failed")
                            : ErrnoTag("socket_configure_failed");
    Reset();
    return false;
  }

  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path_.c_str(),
              socket_path_.size() + 1);
  const socklen_t address_length = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path_.size() + 1);
#if defined(__APPLE__)
  address.sun_len = static_cast<uint8_t>(address_length);
#endif

  const mode_t old_umask = ::umask(0077);
  const int bind_result =
      ::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address),
             address_length);
  ::umask(old_umask);
  if (bind_result != 0) {
    *error = ErrnoTag("socket_bind_failed");
    Reset();
    return false;
  }
  owns_socket_path_ = true;

  if (::chmod(socket_path_.c_str(), 0600) != 0) {
    *error = ErrnoTag("socket_chmod_failed");
    Reset();
    return false;
  }
  if (::listen(listen_fd_, 8) != 0) {
    *error = ErrnoTag("socket_listen_failed");
    Reset();
    return false;
  }
  return true;
}

bool PosixZenzScorerServer::ServeOne(int timeout_msec,
                                     std::string* error) {
  if (error == nullptr || listen_fd_ < 0) {
    return false;
  }
  error->clear();

  pollfd descriptor = {};
  descriptor.fd = listen_fd_;
  descriptor.events = POLLIN;
  int poll_result = 0;
  do {
    poll_result = ::poll(&descriptor, 1, std::max(0, timeout_msec));
  } while (poll_result < 0 && errno == EINTR);

  if (poll_result == 0) {
    return true;
  }
  if (poll_result < 0 ||
      ((descriptor.revents & POLLIN) == 0 &&
       (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)) {
    *error = poll_result < 0 ? ErrnoTag("listener_poll_failed")
                             : "listener_poll_invalid";
    return false;
  }

  int client_fd = -1;
  do {
    client_fd = ::accept(listen_fd_, nullptr, nullptr);
  } while (client_fd < 0 && errno == EINTR);
  if (client_fd < 0) {
    *error = ErrnoTag("socket_accept_failed");
    return false;
  }
  if (!SetCloseOnExec(client_fd) || !DisableSigPipe(client_fd)) {
    *error = ErrnoTag("client_configure_failed");
    ::close(client_fd);
    return false;
  }

  const bool result = HandleClient(client_fd, error);
  ::close(client_fd);
  return result;
}

bool PosixZenzScorerServer::HandleClient(int client_fd,
                                         std::string* error) {
  const auto started = Clock::now();
  bool timed_out = false;
  ZenzWireRequestHeader header = {};
  const Deadline header_deadline =
      Clock::now() + std::chrono::milliseconds(kHeaderReadTimeoutMsec);
  if (!ReadAll(client_fd, &header, sizeof(header), header_deadline,
               &timed_out)) {
    *error = timed_out ? "request_header_timeout"
                       : "request_header_read_failed";
    return true;
  }

  if (header.magic != kZenzWireMagic ||
      header.version != kZenzWireVersion ||
      header.kind != kZenzWireKindRequest) {
    SendResponse(client_fd, header.generation,
                 ErrorResponse("bad_request_header"));
    return true;
  }
  if (header.prompt_size == 0 || header.prompt_size > kMaxPromptBytes) {
    SendResponse(client_fd, header.generation,
                 ErrorResponse(header.prompt_size == 0
                                   ? "empty_prompt"
                                   : "prompt_too_large"));
    return true;
  }

  PosixZenzRequest request;
  request.generation = header.generation;
  request.timeout_msec = std::clamp<uint32_t>(
      header.timeout_msec == 0 ? kMaxRequestTimeoutMsec
                               : header.timeout_msec,
      50, kMaxRequestTimeoutMsec);
  request.max_output_chars = std::clamp<uint32_t>(
      header.max_output_chars == 0 ? kMaxOutputChars
                                   : header.max_output_chars,
      1, kMaxOutputChars);
  request.prompt.resize(header.prompt_size);

  const Deadline request_deadline =
      Clock::now() + std::chrono::milliseconds(request.timeout_msec);
  if (!ReadAll(client_fd, request.prompt.data(), header.prompt_size,
               request_deadline, &timed_out)) {
    SendResponse(client_fd, request.generation,
                 ErrorResponse(timed_out ? "prompt_read_timeout"
                                         : "prompt_read_failed"));
    return true;
  }

  PosixZenzResponse response =
      backend_ ? backend_(request)
               : ErrorResponse("backend_unavailable");
  if (response.status != kZenzWireStatusOk &&
      response.status != kZenzWireStatusError &&
      response.status != kZenzWireStatusTimeout) {
    response = ErrorResponse("backend_invalid_status");
  }
  if (response.value.size() > kMaxResponseValueBytes ||
      response.debug.size() > kMaxResponseDebugBytes) {
    response = ErrorResponse("backend_response_too_large");
  }
  if (response.latency_msec == 0) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - started);
    response.latency_msec = static_cast<uint32_t>(std::min<int64_t>(
        std::max<int64_t>(0, elapsed.count()),
        std::numeric_limits<uint32_t>::max()));
  }

  if (!SendResponse(client_fd, request.generation, response)) {
    *error = "response_write_failed";
  }
  return true;
}

void PosixZenzScorerServer::Reset() {
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (owns_socket_path_) {
    std::string ignored;
    RemoveOwnedSocket(socket_path_, true, &ignored);
    owns_socket_path_ = false;
  }
  if (lock_fd_ >= 0) {
    ::flock(lock_fd_, LOCK_UN);
    ::close(lock_fd_);
    lock_fd_ = -1;
  }
}

}  // namespace zenz_scorer
}  // namespace mozc
