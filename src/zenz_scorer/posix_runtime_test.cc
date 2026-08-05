#include "zenz_scorer/posix_runtime.h"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

#include <sys/socket.h>
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

std::string JoinPath(std::string_view left, std::string_view right) {
  if (left.empty()) {
    return std::string(right);
  }
  if (left.back() == '/') {
    return std::string(left) + std::string(right);
  }
  return std::string(left) + "/" + std::string(right);
}

std::string TestHelperPath() {
  const char* test_srcdir = std::getenv("TEST_SRCDIR");
  const char* test_workspace = std::getenv("TEST_WORKSPACE");
  if (test_srcdir == nullptr || test_workspace == nullptr) {
    return "";
  }
  return JoinPath(JoinPath(test_srcdir, test_workspace),
                  "zenz_scorer/llama_server_process_test_helper");
}

class TemporaryModel final {
 public:
  TemporaryModel() {
    char path[] = "/tmp/mozc_posix_runtime_model_XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd >= 0) {
      constexpr char kContents[] = "test model";
      const ssize_t ignored = ::write(fd, kContents, sizeof(kContents) - 1);
      static_cast<void>(ignored);
      ::close(fd);
      path_ = path;
    }
  }

  ~TemporaryModel() {
    if (!path_.empty()) {
      ::unlink(path_.c_str());
    }
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

std::string MakeTestDirectory() {
  return "/tmp/mozc_posix_runtime_test_" + std::to_string(::geteuid()) + "_" +
         std::to_string(::getpid()) + "_" +
         std::to_string(g_test_sequence.fetch_add(1));
}

void Cleanup(const std::string& directory) {
  ::unlink((directory + "/scorer.sock").c_str());
  ::unlink((directory + "/scorer.lock").c_str());
  ::rmdir(directory.c_str());
}

bool WriteAll(int fd, const void* data, size_t size) {
  const uint8_t* current = static_cast<const uint8_t*>(data);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t result = ::send(fd, current, remaining, 0);
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

PosixScorerRuntimeOptions MakeOptions(const std::string& socket_path,
                                      const std::string& model_path) {
  PosixScorerRuntimeOptions options;
  options.socket_path = socket_path;
  options.llama_server.executable_path = TestHelperPath();
  options.llama_server.model_path = model_path;
  options.llama_server.context_size = 128;
  options.llama_server.threads = 2;
  options.llama_server.readiness_timeout_msec = 3000;
  options.llama_server.readiness_probe_interval_msec = 25;
  options.llama_server.readiness_probe_timeout_msec = 250;
  options.n_predict = 32;
  return options;
}

TEST(PosixScorerRuntimeTest, ServesZenzRequestThroughManagedChild) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());
  ASSERT_FALSE(TestHelperPath().empty());

  const std::string directory = MakeTestDirectory();
  Cleanup(directory);
  const std::string socket_path = directory + "/scorer.sock";

  {
    PosixScorerRuntime runtime(MakeOptions(socket_path, model.path()));
    std::string error;
    ASSERT_TRUE(runtime.Start(&error)) << error;
    EXPECT_TRUE(runtime.running());

    bool client_ok = false;
    std::thread client([&]() {
      const int fd = Connect(socket_path);
      if (fd < 0) {
        return;
      }

      const std::string prompt = "runtime integration prompt";
      ZenzWireRequestHeader request = {};
      request.magic = kZenzWireMagic;
      request.version = kZenzWireVersion;
      request.kind = kZenzWireKindRequest;
      request.generation = 91;
      request.timeout_msec = 1500;
      request.max_output_chars = 32;
      request.prompt_size = static_cast<uint32_t>(prompt.size());

      if (!WriteAll(fd, &request, sizeof(request)) ||
          !WriteAll(fd, prompt.data(), prompt.size())) {
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
                  response.generation == 91 &&
                  response.status == kZenzWireStatusOk && value == "ready" &&
                  debug == "ok";
      ::close(fd);
    });

    EXPECT_TRUE(runtime.ServeOne(2000, &error)) << error;
    client.join();
    EXPECT_TRUE(client_ok);

    runtime.Stop();
    EXPECT_FALSE(runtime.running());
  }

  EXPECT_NE(::access(socket_path.c_str(), F_OK), 0);
  Cleanup(directory);
}

TEST(PosixScorerRuntimeTest, CleansUpChildWhenSocketStartFails) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  const std::string directory = MakeTestDirectory();
  Cleanup(directory);
  const std::string socket_path = directory + "/scorer.sock";

  PosixScorerRuntime first(MakeOptions(socket_path, model.path()));
  std::string error;
  ASSERT_TRUE(first.Start(&error)) << error;

  PosixScorerRuntime second(MakeOptions(socket_path, model.path()));
  EXPECT_FALSE(second.Start(&error));
  EXPECT_EQ(error, "lock_busy");
  EXPECT_FALSE(second.running());

  first.Stop();
  Cleanup(directory);
}

}  // namespace
}  // namespace zenz_scorer
}  // namespace mozc
