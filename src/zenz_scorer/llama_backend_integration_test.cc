#include "zenz_scorer/llama_backend.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "testing/gunit.h"
#include "zenz/zenz_wire_protocol.h"
#include "zenz_scorer/posix_server.h"

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

std::atomic<uint32_t> g_test_sequence{0};

int SendFlags() {
#if defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
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

bool SendAll(int fd, std::string_view data) {
  size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t result =
        ::send(fd, data.data() + offset, data.size() - offset, SendFlags());
    if (result > 0) {
      offset += static_cast<size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

bool WriteAll(int fd, const void* data, size_t size) {
  const uint8_t* current = static_cast<const uint8_t*>(data);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t result = ::send(fd, current, remaining, SendFlags());
    if (result > 0) {
      current += result;
      remaining -= static_cast<size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

bool ReadAll(int fd, void* data, size_t size) {
  uint8_t* current = static_cast<uint8_t*>(data);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t result = ::recv(fd, current, remaining, 0);
    if (result > 0) {
      current += result;
      remaining -= static_cast<size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

size_t ParseContentLength(std::string_view headers) {
  constexpr std::string_view kName = "Content-Length:";
  const size_t position = headers.find(kName);
  if (position == std::string_view::npos) {
    return 0;
  }
  size_t cursor = position + kName.size();
  while (cursor < headers.size() && headers[cursor] == ' ') {
    ++cursor;
  }
  size_t value = 0;
  while (cursor < headers.size() && headers[cursor] >= '0' &&
         headers[cursor] <= '9') {
    value = value * 10 + static_cast<size_t>(headers[cursor] - '0');
    ++cursor;
  }
  return value;
}

bool ReadHttpRequest(int fd, std::string* request) {
  request->clear();
  char buffer[1024];
  size_t expected_size = 0;
  while (request->size() < 64 * 1024) {
    const ssize_t result = ::recv(fd, buffer, sizeof(buffer), 0);
    if (result > 0) {
      request->append(buffer, static_cast<size_t>(result));
      const size_t header_end = request->find("\r\n\r\n");
      if (header_end != std::string::npos) {
        if (expected_size == 0) {
          expected_size = header_end + 4 +
                          ParseContentLength(
                              std::string_view(*request).substr(0, header_end));
        }
        if (request->size() >= expected_size) {
          request->resize(expected_size);
          return true;
        }
      }
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return false;
}

class FakeLlamaServer final {
 public:
  using Handler = std::function<bool(int, const std::string&)>;

  ~FakeLlamaServer() { Join(); }

  bool Start(Handler handler) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0 || !DisableSigPipe(listen_fd_)) {
      return false;
    }

    const int enabled = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled,
                 sizeof(enabled));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::listen(listen_fd_, 1) != 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }

    socklen_t address_length = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address),
                      &address_length) != 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }
    port_ = static_cast<int>(ntohs(address.sin_port));

    thread_ = std::thread([this, handler = std::move(handler)]() mutable {
      int client_fd = -1;
      do {
        client_fd = ::accept(listen_fd_, nullptr, nullptr);
      } while (client_fd < 0 && errno == EINTR);
      if (client_fd < 0 || !DisableSigPipe(client_fd)) {
        if (client_fd >= 0) {
          ::close(client_fd);
        }
        ok_ = false;
        return;
      }

      timeval timeout = {};
      timeout.tv_sec = 2;
      ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout));
      ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout));

      std::string request;
      ok_ = ReadHttpRequest(client_fd, &request) &&
            handler(client_fd, request);
      ::close(client_fd);
    });
    return true;
  }

  int port() const { return port_; }
  bool ok() const { return ok_; }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
  }

 private:
  int listen_fd_ = -1;
  int port_ = 0;
  bool ok_ = false;
  std::thread thread_;
};

std::string MakeTestDirectory() {
  return "/tmp/mozc_zenz_backend_test_" + std::to_string(::geteuid()) + "_" +
         std::to_string(::getpid()) + "_" +
         std::to_string(g_test_sequence.fetch_add(1));
}

void Cleanup(const std::string& directory) {
  ::unlink((directory + "/scorer.sock").c_str());
  ::unlink((directory + "/scorer.lock").c_str());
  ::rmdir(directory.c_str());
}

int ConnectUnixSocket(const std::string& socket_path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0 || !DisableSigPipe(fd)) {
    if (fd >= 0) {
      ::close(fd);
    }
    return -1;
  }
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  const socklen_t length = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path.size() + 1);
#if defined(__APPLE__)
  address.sun_len = static_cast<uint8_t>(length);
#endif
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

struct WireResult {
  bool transport_ok = false;
  ZenzWireResponseHeader header = {};
  std::string value;
  std::string debug;
};

WireResult Exchange(const std::string& socket_path, uint32_t generation,
                    uint32_t timeout_msec, uint32_t max_output_chars,
                    const std::string& prompt) {
  WireResult result;
  const int fd = ConnectUnixSocket(socket_path);
  if (fd < 0) {
    return result;
  }

  ZenzWireRequestHeader request = {};
  request.magic = kZenzWireMagic;
  request.version = kZenzWireVersion;
  request.kind = kZenzWireKindRequest;
  request.generation = generation;
  request.timeout_msec = timeout_msec;
  request.max_output_chars = max_output_chars;
  request.prompt_size = static_cast<uint32_t>(prompt.size());

  if (!WriteAll(fd, &request, sizeof(request)) ||
      !WriteAll(fd, prompt.data(), prompt.size()) ||
      !ReadAll(fd, &result.header, sizeof(result.header))) {
    ::close(fd);
    return result;
  }

  result.value.resize(result.header.value_size);
  result.debug.resize(result.header.debug_size);
  if ((!result.value.empty() &&
       !ReadAll(fd, result.value.data(), result.value.size())) ||
      (!result.debug.empty() &&
       !ReadAll(fd, result.debug.data(), result.debug.size()))) {
    ::close(fd);
    return result;
  }

  result.transport_ok = true;
  ::close(fd);
  return result;
}

TEST(LlamaBackendIntegrationTest, ForwardsUnixSocketRequestToLlamaHttp) {
  constexpr char kApiKey[] = "integration-test-key";
  FakeLlamaServer llama;
  ASSERT_TRUE(llama.Start([](int fd, const std::string& request) {
    EXPECT_NE(request.find("POST /completion HTTP/1.1\r\n"),
              std::string::npos);
    EXPECT_NE(request.find(
                  "Authorization: Bearer integration-test-key\r\n"),
              std::string::npos);
    EXPECT_NE(request.find("\"prompt\":\"prompt-data\""),
              std::string::npos);
    EXPECT_NE(request.find("\"n_predict\":32"), std::string::npos);

    const std::string body =
        "{\"content\":\"converted-result\\nignored\"}";
    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    return SendAll(fd, response);
  }));

  const std::string directory = MakeTestDirectory();
  Cleanup(directory);
  const std::string socket_path = directory + "/scorer.sock";

  LlamaBackendOptions options;
  options.port = llama.port();
  options.api_key = kApiKey;
  options.n_predict = 64;

  {
    PosixZenzScorerServer scorer(socket_path,
                                 CreateLlamaHttpBackend(std::move(options)));
    std::string error;
    ASSERT_TRUE(scorer.Start(&error)) << error;

    WireResult wire_result;
    std::thread client([&]() {
      wire_result = Exchange(socket_path, 91, 1200, 32, "prompt-data");
    });

    EXPECT_TRUE(scorer.ServeOne(2000, &error)) << error;
    client.join();

    ASSERT_TRUE(wire_result.transport_ok);
    EXPECT_EQ(wire_result.header.magic, kZenzWireMagic);
    EXPECT_EQ(wire_result.header.version, kZenzWireVersion);
    EXPECT_EQ(wire_result.header.kind, kZenzWireKindResponse);
    EXPECT_EQ(wire_result.header.generation, 91);
    EXPECT_EQ(wire_result.header.status, kZenzWireStatusOk);
    EXPECT_EQ(wire_result.value, "converted-result");
    EXPECT_EQ(wire_result.debug, "ok");
  }

  llama.Join();
  EXPECT_TRUE(llama.ok());
  Cleanup(directory);
}

TEST(LlamaBackendIntegrationTest, MapsHttpErrorsAndTimeoutsToWireStatus) {
  PosixZenzRequest request;
  request.timeout_msec = 50;
  request.max_output_chars = 32;
  request.prompt = "prompt-data";

  LlamaBackendOptions invalid_options;
  invalid_options.port = 0;
  invalid_options.api_key = "valid-key";
  const PosixZenzResponse invalid =
      CreateLlamaHttpBackend(std::move(invalid_options))(request);
  EXPECT_EQ(invalid.status, kZenzWireStatusError);
  EXPECT_TRUE(invalid.value.empty());
  EXPECT_EQ(invalid.debug, "invalid_port");

  FakeLlamaServer llama;
  ASSERT_TRUE(llama.Start([](int, const std::string&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    return true;
  }));

  LlamaBackendOptions timeout_options;
  timeout_options.port = llama.port();
  timeout_options.api_key = "valid-key";
  const PosixZenzResponse timeout =
      CreateLlamaHttpBackend(std::move(timeout_options))(request);
  llama.Join();

  EXPECT_TRUE(llama.ok());
  EXPECT_EQ(timeout.status, kZenzWireStatusTimeout);
  EXPECT_TRUE(timeout.value.empty());
  EXPECT_EQ(timeout.debug, "http_read_timeout");
}

}  // namespace
}  // namespace zenz_scorer
}  // namespace mozc
