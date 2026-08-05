#if defined(_WIN32)
#error "llama_server_process_test_helper.cc must not be built on Windows"
#endif

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_shutdown_requested = 0;

void HandleSignal(int) { g_shutdown_requested = 1; }

struct Options {
  std::string model_path;
  std::string host;
  std::string api_key;
  std::string test_mode = "ready";
  int port = 0;
  int context_size = 0;
  int threads = 0;
};

bool ParseInteger(const char* text, int* value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(text, &end, 10);
  if (errno != 0 || end == nullptr || *end != '\0' || parsed <= 0 ||
      parsed > 65535) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

bool ParseOptions(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    auto take_value = [&](std::string* output) -> bool {
      if (i + 1 >= argc) {
        return false;
      }
      *output = argv[++i];
      return true;
    };

    if (argument == "-m") {
      if (!take_value(&options->model_path)) return false;
    } else if (argument == "-c") {
      if (i + 1 >= argc || !ParseInteger(argv[++i], &options->context_size)) {
        return false;
      }
    } else if (argument == "-t") {
      if (i + 1 >= argc || !ParseInteger(argv[++i], &options->threads)) {
        return false;
      }
    } else if (argument == "--host") {
      if (!take_value(&options->host)) return false;
    } else if (argument == "--port") {
      if (i + 1 >= argc || !ParseInteger(argv[++i], &options->port)) {
        return false;
      }
    } else if (argument == "--api-key") {
      if (!take_value(&options->api_key)) return false;
    } else if (argument.starts_with("--test-mode=")) {
      options->test_mode = std::string(argument.substr(12));
    } else {
      return false;
    }
  }

  struct stat model_status = {};
  return options->host == "127.0.0.1" && options->port > 0 &&
         options->context_size >= 64 && options->threads > 0 &&
         options->api_key.size() == 64 &&
         ::stat(options->model_path.c_str(), &model_status) == 0 &&
         S_ISREG(model_status.st_mode);
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
    const ssize_t result = ::send(fd, data.data() + offset,
                                  data.size() - offset, 0);
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

int RunServer(const Options& options) {
  const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0 || !DisableSigPipe(listen_fd)) {
    return 20;
  }

  const int enabled = 1;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enabled,
               sizeof(enabled));

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<uint16_t>(options.port));
  if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) != 0 ||
      ::listen(listen_fd, 4) != 0) {
    ::close(listen_fd);
    return 21;
  }

  while (g_shutdown_requested == 0) {
    pollfd descriptor = {};
    descriptor.fd = listen_fd;
    descriptor.events = POLLIN;
    int poll_result;
    do {
      poll_result = ::poll(&descriptor, 1, 100);
    } while (poll_result < 0 && errno == EINTR &&
             g_shutdown_requested == 0);
    if (poll_result <= 0) {
      continue;
    }

    int client_fd;
    do {
      client_fd = ::accept(listen_fd, nullptr, nullptr);
    } while (client_fd < 0 && errno == EINTR &&
             g_shutdown_requested == 0);
    if (client_fd < 0) {
      continue;
    }
    DisableSigPipe(client_fd);

    std::string request;
    if (ReadHttpRequest(client_fd, &request)) {
      const std::string authorization =
          "Authorization: Bearer " + options.api_key + "\r\n";
      const bool authenticated = request.find(authorization) != std::string::npos;
      if (options.test_mode == "never-ready") {
        SendAll(client_fd,
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n");
      } else if (!authenticated) {
        SendAll(client_fd,
                "HTTP/1.1 401 Unauthorized\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n");
      } else {
        constexpr std::string_view kBody = "{\"content\":\"ready\"}";
        const std::string response =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: " + std::to_string(kBody.size()) +
            "\r\nConnection: close\r\n\r\n" + std::string(kBody);
        SendAll(client_fd, response);
      }
    }
    ::close(client_fd);
  }

  ::close(listen_fd);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGTERM, HandleSignal);
  std::signal(SIGINT, HandleSignal);

  Options options;
  if (!ParseOptions(argc, argv, &options)) {
    return 10;
  }
  if (options.test_mode == "exit") {
    return 11;
  }
  return RunServer(options);
}
