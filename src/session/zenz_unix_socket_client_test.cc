#include "session/zenz_unix_socket_client.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "absl/time/clock.h"
#include "testing/gunit.h"
#include "zenz/zenz_wire_protocol.h"

#if !defined(_WIN32)
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif  // !_WIN32

namespace mozc {
namespace session {
namespace {

#if !defined(_WIN32)

using ::mozc::zenz::kZenzWireKindRequest;
using ::mozc::zenz::kZenzWireKindResponse;
using ::mozc::zenz::kZenzWireMagic;
using ::mozc::zenz::kZenzWireStatusOk;
using ::mozc::zenz::kZenzWireVersion;
using ::mozc::zenz::ZenzWireRequestHeader;
using ::mozc::zenz::ZenzWireResponseHeader;

class ScopedSocketDirectory {
 public:
  ScopedSocketDirectory() {
    char directory_template[] = "/tmp/mozc_zenz_socket_test.XXXXXX";
    char* directory = ::mkdtemp(directory_template);
    if (directory != nullptr) {
      directory_ = directory;
      socket_path_ = directory_ + "/scorer.sock";
    }
  }

  ~ScopedSocketDirectory() {
    if (!socket_path_.empty()) {
      ::unlink(socket_path_.c_str());
    }
    if (!directory_.empty()) {
      ::rmdir(directory_.c_str());
    }
  }

  const std::string& directory() const { return directory_; }
  const std::string& socket_path() const { return socket_path_; }

 private:
  std::string directory_;
  std::string socket_path_;
};

bool ReadAll(int fd, void* data, size_t size) {
  uint8_t* current = static_cast<uint8_t*>(data);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t read_size = ::recv(fd, current, remaining, 0);
    if (read_size <= 0) {
      return false;
    }
    current += read_size;
    remaining -= static_cast<size_t>(read_size);
  }
  return true;
}

bool WriteAll(int fd, const void* data, size_t size) {
  const uint8_t* current = static_cast<const uint8_t*>(data);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t written = ::send(fd, current, remaining, 0);
    if (written <= 0) {
      return false;
    }
    current += written;
    remaining -= static_cast<size_t>(written);
  }
  return true;
}

int CreateListeningSocket(const std::string& socket_path) {
  const int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    return -1;
  }

  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  const socklen_t address_length = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path.size() + 1);
#if defined(__APPLE__)
  address.sun_len = static_cast<uint8_t>(address_length);
#endif

  if (::bind(server_fd, reinterpret_cast<const sockaddr*>(&address),
             address_length) != 0 ||
      ::chmod(socket_path.c_str(), 0600) != 0 ||
      ::listen(server_fd, 1) != 0) {
    ::close(server_fd);
    return -1;
  }
  return server_fd;
}

TEST(ZenzUnixSocketClientTest, ExchangesSharedWireProtocol) {
  ScopedSocketDirectory socket_directory;
  ASSERT_FALSE(socket_directory.directory().empty());
  ASSERT_LT(socket_directory.socket_path().size(),
            sizeof(sockaddr_un::sun_path));

  const int server_fd =
      CreateListeningSocket(socket_directory.socket_path());
  ASSERT_GE(server_fd, 0);

  constexpr uint32_t kGeneration = 37;
  const std::string prompt = "zenz prompt payload";
  const std::string expected_value = "zenz result";
  const std::string expected_debug = "fake_server_ok";
  std::atomic<bool> server_ok = false;

  std::thread server([&] {
    pollfd descriptor = {};
    descriptor.fd = server_fd;
    descriptor.events = POLLIN;
    if (::poll(&descriptor, 1, 2000) <= 0) {
      return;
    }

    const int client_fd = ::accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      return;
    }

    ZenzWireRequestHeader request_header = {};
    std::string received_prompt;
    bool ok = ReadAll(client_fd, &request_header, sizeof(request_header));
    if (ok && request_header.prompt_size <= 4096) {
      received_prompt.assign(request_header.prompt_size, '\0');
      if (request_header.prompt_size > 0) {
        ok = ReadAll(client_fd, received_prompt.data(),
                     request_header.prompt_size);
      }
    } else {
      ok = false;
    }

    ok = ok && request_header.magic == kZenzWireMagic &&
         request_header.version == kZenzWireVersion &&
         request_header.kind == kZenzWireKindRequest &&
         request_header.generation == kGeneration &&
         request_header.timeout_msec == 1000 &&
         request_header.max_output_chars == 128 &&
         received_prompt == prompt;

    ZenzWireResponseHeader response_header = {};
    response_header.magic = kZenzWireMagic;
    response_header.version = kZenzWireVersion;
    response_header.kind = kZenzWireKindResponse;
    response_header.generation = kGeneration;
    response_header.status = kZenzWireStatusOk;
    response_header.latency_msec = 7;
    response_header.value_size = static_cast<uint32_t>(expected_value.size());
    response_header.debug_size = static_cast<uint32_t>(expected_debug.size());

    ok = ok && WriteAll(client_fd, &response_header, sizeof(response_header));
    ok = ok && WriteAll(client_fd, expected_value.data(),
                        expected_value.size());
    ok = ok && WriteAll(client_fd, expected_debug.data(),
                        expected_debug.size());
    server_ok.store(ok);
    ::close(client_fd);
  });

  ZenzUnixSocketClient client(socket_directory.socket_path());
  EXPECT_TRUE(client.IsAvailable());

  ZenzLiveRequest request;
  request.generation = kGeneration;
  request.key = "reading";
  request.prompt = prompt;
  request.timeout_msec = 1000;
  request.max_output_chars = 128;
  request.issued_at = absl::Now();

  const ZenzLiveResponse response = client.Convert(request);
  server.join();
  ::close(server_fd);

  EXPECT_TRUE(server_ok.load());
  EXPECT_TRUE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.generation, kGeneration);
  EXPECT_EQ(response.key, request.key);
  EXPECT_EQ(response.value, expected_value);
  EXPECT_EQ(response.debug, expected_debug);
}

TEST(ZenzUnixSocketClientTest, RejectsInvalidSocketPath) {
  ZenzUnixSocketClient client("");
  EXPECT_FALSE(client.IsAvailable());

  ZenzLiveRequest request;
  request.generation = 9;
  const ZenzLiveResponse response = client.Convert(request);
  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.debug, "invalid_socket_path");
}

#else

TEST(ZenzUnixSocketClientTest, IsUnavailableOnWindows) {
  ZenzUnixSocketClient client("unused");
  EXPECT_FALSE(client.IsAvailable());

  ZenzLiveRequest request;
  request.generation = 9;
  const ZenzLiveResponse response = client.Convert(request);
  EXPECT_FALSE(response.ok);
  EXPECT_EQ(response.debug, "unix_socket_not_supported_on_windows");
}

#endif  // !_WIN32

}  // namespace
}  // namespace session
}  // namespace mozc
