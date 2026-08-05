#include "session/zenz_unix_socket_client.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "absl/time/clock.h"
#include "absl/time/time.h"
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

bool ServeOneRequest(int server_fd, const ZenzLiveRequest& expected_request,
                     const std::string& response_value,
                     const std::string& response_debug) {
  pollfd descriptor = {};
  descriptor.fd = server_fd;
  descriptor.events = POLLIN;
  if (::poll(&descriptor, 1, 2000) <= 0) {
    return false;
  }

  const int client_fd = ::accept(server_fd, nullptr, nullptr);
  if (client_fd < 0) {
    return false;
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
       request_header.generation == expected_request.generation &&
       request_header.timeout_msec == expected_request.timeout_msec &&
       request_header.max_output_chars == expected_request.max_output_chars &&
       received_prompt == expected_request.prompt;

  ZenzWireResponseHeader response_header = {};
  response_header.magic = kZenzWireMagic;
  response_header.version = kZenzWireVersion;
  response_header.kind = kZenzWireKindResponse;
  response_header.generation = expected_request.generation;
  response_header.status = kZenzWireStatusOk;
  response_header.latency_msec = 7;
  response_header.value_size = static_cast<uint32_t>(response_value.size());
  response_header.debug_size = static_cast<uint32_t>(response_debug.size());

  ok = ok && WriteAll(client_fd, &response_header, sizeof(response_header));
  ok = ok && WriteAll(client_fd, response_value.data(), response_value.size());
  ok = ok && WriteAll(client_fd, response_debug.data(), response_debug.size());
  ::close(client_fd);
  return ok;
}

ZenzLiveRequest MakeRequest(uint32_t generation, const std::string& prompt,
                            uint32_t timeout_msec) {
  ZenzLiveRequest request;
  request.generation = generation;
  request.key = "reading";
  request.prompt = prompt;
  request.timeout_msec = timeout_msec;
  request.max_output_chars = 128;
  request.issued_at = absl::Now();
  return request;
}

TEST(ZenzUnixSocketClientTest, ExchangesSharedWireProtocolWithoutLaunching) {
  ScopedSocketDirectory socket_directory;
  ASSERT_FALSE(socket_directory.directory().empty());
  ASSERT_LT(socket_directory.socket_path().size(),
            sizeof(sockaddr_un::sun_path));

  const int server_fd =
      CreateListeningSocket(socket_directory.socket_path());
  ASSERT_GE(server_fd, 0);

  const ZenzLiveRequest request =
      MakeRequest(37, "zenz prompt payload", 1000);
  const std::string expected_value = "zenz result";
  const std::string expected_debug = "fake_server_ok";
  std::atomic<bool> server_ok = false;
  std::atomic<int> launch_count = 0;

  std::thread server([&] {
    server_ok.store(ServeOneRequest(server_fd, request, expected_value,
                                    expected_debug));
  });

  ZenzUnixSocketClient client(
      socket_directory.socket_path(),
      [&] {
        ++launch_count;
        return true;
      });
  EXPECT_TRUE(client.IsAvailable());

  const ZenzLiveResponse response = client.Convert(request);
  server.join();
  ::close(server_fd);

  EXPECT_TRUE(server_ok.load());
  EXPECT_EQ(launch_count.load(), 0);
  EXPECT_TRUE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.generation, request.generation);
  EXPECT_EQ(response.key, request.key);
  EXPECT_EQ(response.value, expected_value);
  EXPECT_EQ(response.debug, expected_debug);
}

TEST(ZenzUnixSocketClientTest, LaunchesScorerAndUsesFreshRequestDeadline) {
  ScopedSocketDirectory socket_directory;
  ASSERT_FALSE(socket_directory.directory().empty());

  // The fake scorer deliberately takes longer to create its socket than the
  // inference timeout. Cold-start readiness must therefore use its own budget.
  const ZenzLiveRequest request =
      MakeRequest(41, "cold start prompt", 200);
  const std::string expected_value = "cold start result";
  const std::string expected_debug = "cold_start_ok";
  std::atomic<bool> server_ok = false;
  std::atomic<int> launch_count = 0;
  std::thread server;

  ZenzUnixSocketClient client(
      socket_directory.socket_path(),
      [&] {
        if (launch_count.fetch_add(1) != 0) {
          return true;
        }

        server = std::thread([&] {
          absl::SleepFor(absl::Milliseconds(300));
          const int server_fd =
              CreateListeningSocket(socket_directory.socket_path());
          if (server_fd < 0) {
            return;
          }
          server_ok.store(ServeOneRequest(server_fd, request, expected_value,
                                          expected_debug));
          ::close(server_fd);
        });
        return true;
      },
      absl::Seconds(1));

  const ZenzLiveResponse response = client.Convert(request);
  if (server.joinable()) {
    server.join();
  }

  EXPECT_EQ(launch_count.load(), 1);
  EXPECT_TRUE(server_ok.load());
  EXPECT_TRUE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.value, expected_value);
  EXPECT_EQ(response.debug, expected_debug);
}

TEST(ZenzUnixSocketClientTest, ReportsDefinitiveScorerLaunchFailure) {
  ScopedSocketDirectory socket_directory;
  ASSERT_FALSE(socket_directory.directory().empty());

  std::atomic<int> launch_count = 0;
  ZenzUnixSocketClient client(
      socket_directory.socket_path(),
      [&] {
        ++launch_count;
        return false;
      },
      absl::Seconds(1));

  const ZenzLiveResponse response =
      client.Convert(MakeRequest(42, "launch failure prompt", 1000));

  EXPECT_EQ(launch_count.load(), 1);
  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.debug, "scorer_launch_failed");
}

TEST(ZenzUnixSocketClientTest, TimesOutWhenScorerNeverCreatesSocket) {
  ScopedSocketDirectory socket_directory;
  ASSERT_FALSE(socket_directory.directory().empty());

  std::atomic<int> launch_count = 0;
  ZenzUnixSocketClient client(
      socket_directory.socket_path(),
      [&] {
        ++launch_count;
        return true;
      },
      absl::Milliseconds(80));

  const ZenzLiveResponse response =
      client.Convert(MakeRequest(43, "timeout prompt", 1000));

  EXPECT_EQ(launch_count.load(), 1);
  EXPECT_FALSE(response.ok);
  EXPECT_TRUE(response.timeout);
  EXPECT_EQ(response.debug, "socket_startup_timeout");
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
