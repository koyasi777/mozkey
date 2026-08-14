#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "zenz/zenz_unix_socket_path.h"
#include "zenz/zenz_wire_protocol.h"

namespace {

using ::mozc::zenz::kZenzWireKindRequest;
using ::mozc::zenz::kZenzWireKindResponse;
using ::mozc::zenz::kZenzWireMagic;
using ::mozc::zenz::kZenzWireStatusOk;
using ::mozc::zenz::kZenzWireVersion;
using ::mozc::zenz::ZenzWireRequestHeader;
using ::mozc::zenz::ZenzWireResponseHeader;

constexpr uint32_t kGeneration = 0x5A4E5A31;
constexpr uint32_t kRequestTimeoutMsec = 60000;
constexpr uint32_t kMaximumOutputChars = 64;
// The package verifier must outlive the production scorer's bounded cold-start
// readiness window so it can observe the socket after a near-deadline success.
constexpr int kStartupTimeoutMsec = 240000;

constexpr char kProbePrompt[] =
    "\xEE\xB8\x82\xEE\xB8\x80\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88"
    "\xEE\xB8\x81";

class ScopedFd final {
 public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  ScopedFd(const ScopedFd &) = delete;
  ScopedFd &operator=(const ScopedFd &) = delete;

  int get() const { return fd_; }

 private:
  int fd_;
};

bool WriteAll(int fd, const void *data, size_t size) {
  const auto *current = static_cast<const uint8_t *>(data);
  size_t remaining = size;

  while (remaining > 0) {
    const ssize_t written = ::send(fd, current, remaining, 0);
    if (written > 0) {
      current += written;
      remaining -= static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }

  return true;
}

bool ReadAll(int fd, void *data, size_t size) {
  auto *current = static_cast<uint8_t *>(data);
  size_t remaining = size;

  while (remaining > 0) {
    const ssize_t received = ::recv(fd, current, remaining, 0);
    if (received > 0) {
      current += received;
      remaining -= static_cast<size_t>(received);
      continue;
    }
    if (received < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }

  return true;
}

int ConnectOnce(const std::string &socket_path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  sockaddr_un address = {};
  address.sun_family = AF_UNIX;

  if (socket_path.size() >= sizeof(address.sun_path)) {
    ::close(fd);
    errno = ENAMETOOLONG;
    return -1;
  }

  std::memcpy(
      address.sun_path,
      socket_path.c_str(),
      socket_path.size() + 1);

  if (::connect(
          fd,
          reinterpret_cast<const sockaddr *>(&address),
          sizeof(address)) != 0) {
    const int saved_errno = errno;
    ::close(fd);
    errno = saved_errno;
    return -1;
  }

  return fd;
}

int WaitForConnection(const std::string &socket_path) {
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(kStartupTimeoutMsec);

  int last_errno = 0;

  while (std::chrono::steady_clock::now() < deadline) {
    const int fd = ConnectOnce(socket_path);
    if (fd >= 0) {
      return fd;
    }

    last_errno = errno;

    if (last_errno != ENOENT &&
        last_errno != ECONNREFUSED &&
        last_errno != EAGAIN) {
      std::cerr
          << "Unexpected socket connect error: "
          << std::strerror(last_errno)
          << '\n';
      return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  errno = last_errno != 0 ? last_errno : ETIMEDOUT;
  return -1;
}

bool SetIoTimeout(int fd) {
  timeval timeout = {};
  timeout.tv_sec = kRequestTimeoutMsec / 1000;
  timeout.tv_usec = (kRequestTimeoutMsec % 1000) * 1000;

  return ::setsockopt(
             fd,
             SOL_SOCKET,
             SO_SNDTIMEO,
             &timeout,
             sizeof(timeout)) == 0 &&
         ::setsockopt(
             fd,
             SOL_SOCKET,
             SO_RCVTIMEO,
             &timeout,
             sizeof(timeout)) == 0;
}

int AssertUnavailable() {
  const std::string socket_path =
      ::mozc::zenz::GetZenzUnixSocketPath();

  if (socket_path.empty()) {
    std::cerr << "Zenz socket path is empty\n";
    return 1;
  }

  std::cout << "Socket path = " << socket_path << '\n';

  const int fd = ConnectOnce(socket_path);
  if (fd >= 0) {
    ::close(fd);
    std::cerr
        << "A Zenz scorer is already accepting connections. "
        << "Refusing to risk testing the wrong process.\n";
    return 1;
  }

  if (errno != ENOENT && errno != ECONNREFUSED) {
    std::cerr
        << "Unexpected pre-launch socket state: "
        << std::strerror(errno)
        << '\n';
    return 1;
  }

  std::cout << "Pre-launch scorer availability = ABSENT\n";
  return 0;
}

int SendProbeRequest() {
  const std::string socket_path =
      ::mozc::zenz::GetZenzUnixSocketPath();

  if (socket_path.empty()) {
    std::cerr << "Zenz socket path is empty\n";
    return 1;
  }

  std::cout << "Socket path = " << socket_path << '\n';

  const int raw_fd = WaitForConnection(socket_path);
  if (raw_fd < 0) {
    std::cerr
        << "Timed out waiting for packaged scorer: "
        << std::strerror(errno)
        << '\n';
    return 1;
  }

  ScopedFd fd(raw_fd);

  if (!SetIoTimeout(fd.get())) {
    std::cerr << "Failed to configure socket I/O timeout\n";
    return 1;
  }

  const std::string_view prompt(kProbePrompt, sizeof(kProbePrompt) - 1);

  ZenzWireRequestHeader request = {};
  request.magic = kZenzWireMagic;
  request.version = kZenzWireVersion;
  request.kind = kZenzWireKindRequest;
  request.generation = kGeneration;
  request.timeout_msec = kRequestTimeoutMsec;
  request.max_output_chars = kMaximumOutputChars;
  request.prompt_size = static_cast<uint32_t>(prompt.size());

  if (!WriteAll(fd.get(), &request, sizeof(request)) ||
      !WriteAll(fd.get(), prompt.data(), prompt.size())) {
    std::cerr << "Failed to write Zenz request\n";
    return 1;
  }

  ZenzWireResponseHeader response = {};

  if (!ReadAll(fd.get(), &response, sizeof(response))) {
    std::cerr << "Failed to read Zenz response header\n";
    return 1;
  }

  if (response.magic != kZenzWireMagic ||
      response.version != kZenzWireVersion ||
      response.kind != kZenzWireKindResponse ||
      response.generation != kGeneration) {
    std::cerr << "Invalid Zenz response header\n";
    return 1;
  }

  constexpr uint32_t kMaxPayloadBytes = 1024 * 1024;

  if (response.value_size > kMaxPayloadBytes ||
      response.debug_size > kMaxPayloadBytes) {
    std::cerr << "Zenz response payload is unexpectedly large\n";
    return 1;
  }

  std::string value(response.value_size, '\0');
  std::string debug(response.debug_size, '\0');

  if ((!value.empty() &&
       !ReadAll(fd.get(), value.data(), value.size())) ||
      (!debug.empty() &&
       !ReadAll(fd.get(), debug.data(), debug.size()))) {
    std::cerr << "Failed to read Zenz response payload\n";
    return 1;
  }

  std::cout << "Response status = " << response.status << '\n';
  std::cout << "Response value bytes = " << value.size() << '\n';

  if (!debug.empty()) {
    std::cout << "Response debug = " << debug << '\n';
  }

  if (response.status != kZenzWireStatusOk) {
    std::cerr << "Packaged scorer returned non-OK status\n";
    return 1;
  }

  std::cout << "Packaged scorer wire request = PASS\n";
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr
        << "Usage: " << argv[0]
        << " --assert-unavailable | --request\n";
    return 2;
  }

  const std::string_view mode(argv[1]);

  if (mode == "--assert-unavailable") {
    return AssertUnavailable();
  }

  if (mode == "--request") {
    return SendProbeRequest();
  }

  std::cerr << "Unknown mode: " << mode << '\n';
  return 2;
}
