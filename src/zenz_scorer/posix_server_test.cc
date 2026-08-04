#include "zenz_scorer/posix_server.h"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "testing/gunit.h"
#include "zenz/zenz_wire_protocol.h"

namespace mozc {
namespace zenz_scorer {
namespace {

using ::mozc::zenz::kZenzWireKindRequest;
using ::mozc::zenz::kZenzWireKindResponse;
using ::mozc::zenz::kZenzWireMagic;
using ::mozc::zenz::kZenzWireStatusOk;
using ::mozc::zenz::kZenzWireVersion;
using ::mozc::zenz::ZenzWireRequestHeader;
using ::mozc::zenz::ZenzWireResponseHeader;

std::atomic<uint32_t> g_test_sequence{0};

std::string MakeTestDirectory() {
  return "/tmp/mozc_zenz_scorer_test_" + std::to_string(::geteuid()) + "_" +
         std::to_string(::getpid()) + "_" +
         std::to_string(g_test_sequence.fetch_add(1));
}

void Cleanup(const std::string& directory) {
  ::unlink((directory + "/scorer.sock").c_str());
  ::unlink((directory + "/scorer.lock").c_str());
  ::rmdir(directory.c_str());
}

bool WriteChunks(int fd, const void* data, size_t size) {
  const uint8_t* current = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    ssize_t result = -1;
    do {
      result = ::send(fd, current + i, 1, 0);
    } while (result < 0 && errno == EINTR);
    if (result != 1) {
      return false;
    }
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

int Connect(const std::string& socket_path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
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

TEST(PosixZenzScorerServerTest, CreatesPrivateSocketAndLock) {
  const std::string directory = MakeTestDirectory();
  Cleanup(directory);
  const std::string socket_path = directory + "/scorer.sock";

  {
    PosixZenzScorerServer server(socket_path, {});
    std::string error;
    ASSERT_TRUE(server.Start(&error)) << error;

    struct stat directory_status = {};
    ASSERT_EQ(::lstat(directory.c_str(), &directory_status), 0);
    EXPECT_TRUE(S_ISDIR(directory_status.st_mode));
    EXPECT_EQ(directory_status.st_uid, ::geteuid());
    EXPECT_EQ(directory_status.st_mode & 0777, 0700);

    struct stat socket_status = {};
    ASSERT_EQ(::lstat(socket_path.c_str(), &socket_status), 0);
    EXPECT_TRUE(S_ISSOCK(socket_status.st_mode));
    EXPECT_EQ(socket_status.st_uid, ::geteuid());
    EXPECT_EQ(socket_status.st_mode & 0777, 0600);

    struct stat lock_status = {};
    ASSERT_EQ(::lstat((directory + "/scorer.lock").c_str(), &lock_status), 0);
    EXPECT_TRUE(S_ISREG(lock_status.st_mode));
    EXPECT_EQ(lock_status.st_mode & 0777, 0600);
  }

  struct stat removed_socket_status = {};
  EXPECT_NE(::lstat(socket_path.c_str(), &removed_socket_status), 0);
  Cleanup(directory);
}

TEST(PosixZenzScorerServerTest, ServesWireRequestWithPartialWrites) {
  const std::string directory = MakeTestDirectory();
  Cleanup(directory);
  const std::string socket_path = directory + "/scorer.sock";

  {
    PosixZenzScorerServer server(
        socket_path, [](const PosixZenzRequest& request) {
          EXPECT_EQ(request.generation, 73);
          EXPECT_EQ(request.timeout_msec, 1200);
          EXPECT_EQ(request.max_output_chars, 48);
          EXPECT_EQ(request.prompt, "prompt-data");
          PosixZenzResponse response;
          response.status = kZenzWireStatusOk;
          response.value = "converted";
          response.debug = "test_backend";
          return response;
        });
    std::string error;
    ASSERT_TRUE(server.Start(&error)) << error;

    bool client_ok = false;
    std::thread client([&]() {
      const int fd = Connect(socket_path);
      if (fd < 0) {
        return;
      }

      const std::string prompt = "prompt-data";
      ZenzWireRequestHeader request = {};
      request.magic = kZenzWireMagic;
      request.version = kZenzWireVersion;
      request.kind = kZenzWireKindRequest;
      request.generation = 73;
      request.timeout_msec = 1200;
      request.max_output_chars = 48;
      request.prompt_size = static_cast<uint32_t>(prompt.size());

      if (!WriteChunks(fd, &request, sizeof(request)) ||
          !WriteChunks(fd, prompt.data(), prompt.size())) {
        ::close(fd);
        return;
      }

      ZenzWireResponseHeader response = {};
      if (!ReadAll(fd, &response, sizeof(response))) {
        ::close(fd);
        return;
      }
      std::string value(response.value_size, '\0');
      std::string debug(response.debug_size, '\0');
      if ((!value.empty() && !ReadAll(fd, value.data(), value.size())) ||
          (!debug.empty() && !ReadAll(fd, debug.data(), debug.size()))) {
        ::close(fd);
        return;
      }

      client_ok = response.magic == kZenzWireMagic &&
                  response.version == kZenzWireVersion &&
                  response.kind == kZenzWireKindResponse &&
                  response.generation == 73 &&
                  response.status == kZenzWireStatusOk &&
                  value == "converted" && debug == "test_backend";
      ::close(fd);
    });

    EXPECT_TRUE(server.ServeOne(2000, &error)) << error;
    client.join();
    EXPECT_TRUE(client_ok);
  }
  Cleanup(directory);
}

TEST(PosixZenzScorerServerTest, RejectsSecondServerForSameUserEndpoint) {
  const std::string directory = MakeTestDirectory();
  Cleanup(directory);
  const std::string socket_path = directory + "/scorer.sock";

  {
    PosixZenzScorerServer first(socket_path, {});
    std::string error;
    ASSERT_TRUE(first.Start(&error)) << error;

    PosixZenzScorerServer second(socket_path, {});
    EXPECT_FALSE(second.Start(&error));
    EXPECT_EQ(error, "lock_busy");
  }
  Cleanup(directory);
}

}  // namespace
}  // namespace zenz_scorer
}  // namespace mozc
