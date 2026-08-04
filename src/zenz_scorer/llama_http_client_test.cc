#include "zenz_scorer/llama_http_client.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "testing/gunit.h"

namespace mozc {
namespace zenz_scorer {
namespace {

using Handler = std::function<bool(int, const std::string&)>;

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

int SendFlags() {
#if defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

bool SendAll(int fd, std::string_view data, bool one_byte_chunks) {
  size_t offset = 0;
  while (offset < data.size()) {
    const size_t requested = one_byte_chunks ? 1 : data.size() - offset;
    const ssize_t result =
        ::send(fd, data.data() + offset, requested, SendFlags());
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


std::string HexSize(size_t size) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%zx", size);
  return buffer;
}

bool ParseContentLength(std::string_view headers, size_t* content_length) {
  constexpr std::string_view kHeader = "Content-Length:";
  const size_t position = headers.find(kHeader);
  if (position == std::string_view::npos) {
    return false;
  }
  size_t current = position + kHeader.size();
  while (current < headers.size() &&
         (headers[current] == ' ' || headers[current] == '\t')) {
    ++current;
  }
  size_t parsed = 0;
  bool found_digit = false;
  while (current < headers.size() &&
         '0' <= headers[current] && headers[current] <= '9') {
    found_digit = true;
    parsed = parsed * 10 + static_cast<size_t>(headers[current] - '0');
    ++current;
  }
  if (!found_digit) {
    return false;
  }
  *content_length = parsed;
  return true;
}

bool ReadHttpRequest(int fd, std::string* request) {
  request->clear();
  char buffer[256] = {};
  size_t expected_size = 0;
  while (true) {
    const ssize_t result = ::recv(fd, buffer, sizeof(buffer), 0);
    if (result > 0) {
      request->append(buffer, static_cast<size_t>(result));
      const size_t header_end = request->find("\r\n\r\n");
      if (header_end != std::string::npos && expected_size == 0) {
        size_t content_length = 0;
        if (!ParseContentLength(
                std::string_view(*request).substr(0, header_end + 4),
                &content_length)) {
          return false;
        }
        expected_size = header_end + 4 + content_length;
      }
      if (expected_size != 0 && request->size() >= expected_size) {
        request->resize(expected_size);
        return true;
      }
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
}

class FakeHttpServer final {
 public:
  FakeHttpServer() = default;
  ~FakeHttpServer() {
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  FakeHttpServer(const FakeHttpServer&) = delete;
  FakeHttpServer& operator=(const FakeHttpServer&) = delete;

  bool Start(Handler handler) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0 || !DisableSigPipe(listen_fd_)) {
      return false;
    }

    const int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::listen(listen_fd_, 1) != 0) {
      return false;
    }

    socklen_t address_size = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address),
                      &address_size) != 0) {
      return false;
    }
    port_ = ntohs(address.sin_port);

    thread_ = std::thread([this, handler = std::move(handler)]() {
      int client_fd = -1;
      do {
        client_fd = ::accept(listen_fd_, nullptr, nullptr);
      } while (client_fd < 0 && errno == EINTR);
      if (client_fd < 0) {
        return;
      }
      DisableSigPipe(client_fd);
      std::string request;
      const bool read_ok = ReadHttpRequest(client_fd, &request);
      ok_.store(read_ok && handler(client_fd, request));
      ::close(client_fd);
    });
    return true;
  }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int port() const { return port_; }
  bool ok() const { return ok_.load(); }

 private:
  int listen_fd_ = -1;
  int port_ = 0;
  std::atomic<bool> ok_{false};
  std::thread thread_;
};

LlamaHttpCompletionRequest MakeRequest(int port) {
  LlamaHttpCompletionRequest request;
  request.port = port;
  request.api_key = "test-key";
  request.n_predict = 64;
  request.timeout_msec = 1000;
  request.max_output_chars = 8;
  request.prompt = "a\"b\n日本";
  return request;
}

TEST(LlamaHttpClientTest, PostsCompletionAndParsesPartialResponse) {
  FakeHttpServer server;
  ASSERT_TRUE(server.Start([](int fd, const std::string& request) {
    const bool request_ok =
        request.find("POST /completion HTTP/1.1\r\n") == 0 &&
        request.find("Authorization: Bearer test-key\r\n") !=
            std::string::npos &&
        request.find("\"prompt\":\"a\\\"b\\n日本\"") !=
            std::string::npos &&
        request.find("\"n_predict\":8") != std::string::npos &&
        request.find("\"stream\":false") != std::string::npos;

    const std::string body =
        "{\"content\":\"変換結果\\nignored\"}";
    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
        body;
    return request_ok && SendAll(fd, response, true);
  }));

  const LlamaHttpCompletionResponse response =
      PostLlamaHttpCompletion(MakeRequest(server.port()));
  server.Join();

  EXPECT_TRUE(server.ok());
  EXPECT_EQ(response.status, LlamaHttpCompletionStatus::kOk);
  EXPECT_EQ(response.value, "変換結果");
  EXPECT_EQ(response.debug, "ok");
}

TEST(LlamaHttpClientTest, ParsesChunkedUnicodeResponse) {
  FakeHttpServer server;
  ASSERT_TRUE(server.Start([](int fd, const std::string&) {
    const std::string first = "{\"content\":\"\\u65e5";
    const std::string second = "\\u672c\\ud83d\\ude00\"}";
    std::string response =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n";
    response += HexSize(first.size()) + "\r\n" + first + "\r\n";
    response += HexSize(second.size()) + "\r\n" + second +
                "\r\n0\r\n\r\n";
    return SendAll(fd, response, true);
  }));

  LlamaHttpCompletionRequest request = MakeRequest(server.port());
  request.max_output_chars = 3;
  const LlamaHttpCompletionResponse response =
      PostLlamaHttpCompletion(request);
  server.Join();

  EXPECT_TRUE(server.ok());
  EXPECT_EQ(response.status, LlamaHttpCompletionStatus::kOk);
  EXPECT_EQ(response.value, "日本😀");
}

TEST(LlamaHttpClientTest, TimesOutWaitingForResponse) {
  FakeHttpServer server;
  ASSERT_TRUE(server.Start([](int, const std::string&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    return true;
  }));

  LlamaHttpCompletionRequest request = MakeRequest(server.port());
  request.timeout_msec = 50;
  const LlamaHttpCompletionResponse response =
      PostLlamaHttpCompletion(request);
  server.Join();

  EXPECT_TRUE(server.ok());
  EXPECT_EQ(response.status, LlamaHttpCompletionStatus::kTimeout);
  EXPECT_EQ(response.debug, "http_read_timeout");
}

TEST(LlamaHttpClientTest, RejectsOversizedResponseBeforeReadingBody) {
  FakeHttpServer server;
  ASSERT_TRUE(server.Start([](int fd, const std::string&) {
    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Length: 65537\r\n"
        "Connection: close\r\n\r\n";
    return SendAll(fd, response, false);
  }));

  const LlamaHttpCompletionResponse response =
      PostLlamaHttpCompletion(MakeRequest(server.port()));
  server.Join();

  EXPECT_TRUE(server.ok());
  EXPECT_EQ(response.status, LlamaHttpCompletionStatus::kError);
  EXPECT_EQ(response.debug, "http_response_too_large");
}

TEST(LlamaHttpClientTest, RejectsHeaderInjectionInApiKey) {
  LlamaHttpCompletionRequest request = MakeRequest(12345);
  request.api_key = "bad\r\nInjected: yes";
  const LlamaHttpCompletionResponse response =
      PostLlamaHttpCompletion(request);

  EXPECT_EQ(response.status, LlamaHttpCompletionStatus::kError);
  EXPECT_EQ(response.debug, "invalid_api_key");
}

}  // namespace
}  // namespace zenz_scorer
}  // namespace mozc
