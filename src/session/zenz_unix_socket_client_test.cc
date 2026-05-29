#include "session/zenz_unix_socket_client.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

constexpr char kDefaultNamedPipeEndpoint[] = "\\\\.\\pipe\\mozc_zenz_scorer";
constexpr char kDefaultUnixSocketEndpoint[] = "/tmp/mozc_zenz_scorer.sock";

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

#if defined(__APPLE__)
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

int CreateListeningSocket(const std::string& socket_path) {
  ::unlink(socket_path.c_str());

  const int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  EXPECT_GE(server_fd, 0);
  if (server_fd < 0) {
    return -1;
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

  const socklen_t addr_len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path.size() + 1);
  EXPECT_EQ(::bind(server_fd, reinterpret_cast<const sockaddr*>(&addr), addr_len), 0);
  if (::listen(server_fd, 1) != 0) {
    ::close(server_fd);
    return -1;
  }
  return server_fd;
}
#endif

TEST(ZenzUnixSocketClientTest, AvailabilityMatchesPlatform) {
  ZenzUnixSocketClient client;
#if defined(__APPLE__)
  EXPECT_TRUE(client.IsAvailable());
#else
  EXPECT_FALSE(client.IsAvailable());
#endif
}

TEST(ZenzUnixSocketClientTest, ConvertFailsWithoutServer) {
  ZenzUnixSocketClient client;
  ZenzLiveRequest request;
  request.generation = 7;
  request.key = "てすと";
  request.prompt = "prompt";
  request.pipe_name = "/tmp/does_not_exist_mozc_zenz.sock";
  request.timeout_msec = 50;

  const ZenzLiveResponse response = client.Convert(request);
#if defined(__APPLE__)
  EXPECT_FALSE(response.ok);
  EXPECT_EQ(response.debug, "socket_connect_failed");
#else
  EXPECT_EQ(response.debug, "unix_socket_only_supported_on_macos");
#endif
}

#if defined(__APPLE__)
TEST(ZenzUnixSocketClientTest, ConvertUsesDefaultSocketForNamedPipeEndpoint) {
  const int server_fd = CreateListeningSocket(kDefaultUnixSocketEndpoint);
  ASSERT_GE(server_fd, 0);

  std::string received_prompt;
  std::thread server([&]() {
    const int client_fd = ::accept(server_fd, nullptr, nullptr);
    ASSERT_GE(client_fd, 0);

    ZenzWireRequestHeader request_header = {};
    ASSERT_TRUE(ReadAll(client_fd, &request_header, sizeof(request_header)));
    EXPECT_EQ(request_header.magic, kZenzWireMagic);
    EXPECT_EQ(request_header.version, kZenzWireVersion);
    EXPECT_EQ(request_header.kind, kZenzWireKindRequest);
    EXPECT_EQ(request_header.generation, 42u);

    received_prompt.assign(request_header.prompt_size, '\0');
    ASSERT_TRUE(ReadAll(client_fd,
                        received_prompt.data(),
                        request_header.prompt_size));

    const std::string value = "彼は天敵です";
    const std::string debug = "ok";
    ZenzWireResponseHeader response_header = {};
    response_header.magic = kZenzWireMagic;
    response_header.version = kZenzWireVersion;
    response_header.kind = kZenzWireKindResponse;
    response_header.generation = 42;
    response_header.status = 0;
    response_header.latency_msec = 3;
    response_header.value_size = static_cast<uint32_t>(value.size());
    response_header.debug_size = static_cast<uint32_t>(debug.size());

    ASSERT_TRUE(WriteAll(client_fd, &response_header, sizeof(response_header)));
    ASSERT_TRUE(WriteAll(client_fd, value.data(), value.size()));
    ASSERT_TRUE(WriteAll(client_fd, debug.data(), debug.size()));

    ::close(client_fd);
    ::close(server_fd);
    ::unlink(kDefaultUnixSocketEndpoint);
  });

  ZenzUnixSocketClient client;
  ZenzLiveRequest request;
  request.generation = 42;
  request.key = "かれはてんてきです";
  request.prompt = "sample-prompt";
  request.pipe_name = kDefaultNamedPipeEndpoint;
  request.timeout_msec = 100;
  request.max_output_chars = 256;

  const ZenzLiveResponse response = client.Convert(request);
  server.join();

  EXPECT_TRUE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.key, request.key);
  EXPECT_EQ(response.value, "彼は天敵です");
  EXPECT_EQ(response.debug, "ok");
  EXPECT_EQ(received_prompt, request.prompt);
}
#endif

}  // namespace
}  // namespace session
}  // namespace mozc