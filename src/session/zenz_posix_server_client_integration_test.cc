#include "session/zenz_unix_socket_client.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

#include <unistd.h>

#include "absl/time/clock.h"
#include "testing/gunit.h"
#include "zenz/zenz_unix_socket_path.h"
#include "zenz/zenz_wire_protocol.h"
#include "zenz_scorer/posix_server.h"

namespace mozc {
namespace session {
namespace {

// Uses a private synthetic endpoint, not the installed Mozkey scorer socket.
class ScopedTestEndpoint final {
 public:
  ScopedTestEndpoint() {
    char path[] = "/tmp/mozkey_zenz_bridge_test_XXXXXX";
    char* directory = ::mkdtemp(path);
    if (directory != nullptr) {
      directory_ = directory;
      socket_path_ = directory_ + "/scorer.sock";
    }
  }

  ~ScopedTestEndpoint() {
    if (!socket_path_.empty()) {
      ::unlink(socket_path_.c_str());
    }
    if (!directory_.empty()) {
      ::unlink((directory_ + "/scorer.lock").c_str());
      ::rmdir(directory_.c_str());
    }
  }

  const std::string& directory() const { return directory_; }
  const std::string& socket_path() const { return socket_path_; }

 private:
  std::string directory_;
  std::string socket_path_;
};

// The process environment is changed only within this test binary.  No real
// login session or installed IME is modified.  Tests using this fixture run
// serially, as regular GoogleTest cases do by default.
#if defined(__linux__)
class ScopedXdgRuntimeDir final {
 public:
  explicit ScopedXdgRuntimeDir(const char* value) {
    const char* previous = ::getenv("XDG_RUNTIME_DIR");
    if (previous != nullptr) {
      had_original_ = true;
      original_ = previous;
    }
    ok_ = value == nullptr ? ::unsetenv("XDG_RUNTIME_DIR") == 0
                           : ::setenv("XDG_RUNTIME_DIR", value, 1) == 0;
  }

  ~ScopedXdgRuntimeDir() {
    if (had_original_) {
      ::setenv("XDG_RUNTIME_DIR", original_.c_str(), 1);
    } else {
      ::unsetenv("XDG_RUNTIME_DIR");
    }
  }

  ScopedXdgRuntimeDir(const ScopedXdgRuntimeDir&) = delete;
  ScopedXdgRuntimeDir& operator=(const ScopedXdgRuntimeDir&) = delete;
  bool ok() const { return ok_; }

 private:
  bool had_original_ = false;
  bool ok_ = false;
  std::string original_;
};

class ScopedSyntheticRuntimeRoot final {
 public:
  ScopedSyntheticRuntimeRoot() {
    char pattern[] = "/tmp/mozkey-z1a-xdg-integration-XXXXXX";
    char* created = ::mkdtemp(pattern);
    if (created != nullptr) {
      path_ = created;
    }
  }

  ~ScopedSyntheticRuntimeRoot() {
    if (path_.empty()) {
      return;
    }
    const std::string child = path_ + "/mozkey-zenz";
    ::unlink((child + "/scorer.sock").c_str());
    ::unlink((child + "/scorer.lock").c_str());
    ::rmdir(child.c_str());
    ::rmdir(path_.c_str());
  }

  ScopedSyntheticRuntimeRoot(const ScopedSyntheticRuntimeRoot&) = delete;
  ScopedSyntheticRuntimeRoot& operator=(
      const ScopedSyntheticRuntimeRoot&) = delete;
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};
#endif  // __linux__

ZenzLiveRequest SyntheticRequest(uint32_t generation,
                                 const std::string& prompt) {
  ZenzLiveRequest request;
  request.generation = generation;
  request.key = "synthetic-reading";
  request.prompt = prompt;
  request.timeout_msec = 1000;
  request.max_output_chars = 64;
  request.issued_at = absl::Now();
  return request;
}

TEST(ZenzPosixServerClientIntegrationTest, ExchangesTwoIndependentRequests) {
  ScopedTestEndpoint endpoint;
  ASSERT_FALSE(endpoint.directory().empty());

  std::atomic<int> backend_calls{0};
  zenz_scorer::PosixZenzScorerServer server(
      endpoint.socket_path(),
      [&](const zenz_scorer::PosixZenzRequest& request) {
        ++backend_calls;
        zenz_scorer::PosixZenzResponse response;
        response.status = zenz::kZenzWireStatusOk;
        response.value = "mock:" + request.prompt;
        response.debug = "synthetic_backend";
        return response;
      });
  std::string error;
  ASSERT_TRUE(server.Start(&error)) << error;

  bool serve_ok = true;
  std::string serve_error;
  std::thread server_thread([&] {
    for (int i = 0; i < 2; ++i) {
      if (!server.ServeOne(2000, &serve_error)) {
        serve_ok = false;
        break;
      }
    }
  });

  ZenzUnixSocketClient client(endpoint.socket_path());
  const ZenzLiveResponse first =
      client.Convert(SyntheticRequest(41, "first synthetic prompt"));
  const ZenzLiveResponse second =
      client.Convert(SyntheticRequest(42, "second synthetic prompt"));
  server_thread.join();

  EXPECT_TRUE(serve_ok) << serve_error;
  EXPECT_EQ(backend_calls.load(), 2);
  EXPECT_TRUE(first.ok) << first.debug;
  EXPECT_FALSE(first.timeout);
  EXPECT_EQ(first.generation, 41);
  EXPECT_EQ(first.key, "synthetic-reading");
  EXPECT_EQ(first.value, "mock:first synthetic prompt");
  EXPECT_EQ(first.debug, "synthetic_backend");
  EXPECT_TRUE(second.ok) << second.debug;
  EXPECT_FALSE(second.timeout);
  EXPECT_EQ(second.generation, 42);
  EXPECT_EQ(second.key, "synthetic-reading");
  EXPECT_EQ(second.value, "mock:second synthetic prompt");
  EXPECT_EQ(second.debug, "synthetic_backend");
}

TEST(ZenzPosixServerClientIntegrationTest, NoBackendReturnsProtocolError) {
  ScopedTestEndpoint endpoint;
  ASSERT_FALSE(endpoint.directory().empty());
  zenz_scorer::PosixZenzScorerServer server(endpoint.socket_path(), {});
  std::string error;
  ASSERT_TRUE(server.Start(&error)) << error;

  bool serve_ok = false;
  std::string serve_error;
  std::thread server_thread([&] {
    serve_ok = server.ServeOne(2000, &serve_error);
  });
  ZenzUnixSocketClient client(endpoint.socket_path());
  const ZenzLiveResponse response =
      client.Convert(SyntheticRequest(91, "backend absent"));
  server_thread.join();

  EXPECT_TRUE(serve_ok) << serve_error;
  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.generation, 91);
  EXPECT_EQ(response.key, "synthetic-reading");
  EXPECT_EQ(response.debug, "backend_unavailable");
}

#if defined(__linux__)
// Proves the *production path resolver* selects the same socket for both
// sides, without launching a scorer executable or loading a GGUF model.
TEST(ZenzPosixServerClientIntegrationTest, LinuxXdgEndpointRoundTrip) {
  ScopedSyntheticRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  ScopedXdgRuntimeDir environment(root.path().c_str());
  ASSERT_TRUE(environment.ok());

  const std::string socket_path = zenz::GetZenzUnixSocketPath();
  ASSERT_EQ(socket_path, root.path() + "/mozkey-zenz/scorer.sock");
  ASSERT_EQ(zenz::GetZenzUnixSocketDirectory(), root.path() + "/mozkey-zenz");

  std::atomic<int> backend_calls{0};
  zenz_scorer::PosixZenzScorerServer server(
      socket_path, [&](const zenz_scorer::PosixZenzRequest& request) {
        ++backend_calls;
        zenz_scorer::PosixZenzResponse response;
        response.status = zenz::kZenzWireStatusOk;
        response.value = "mock:" + request.prompt;
        return response;
      });
  std::string error;
  ASSERT_TRUE(server.Start(&error)) << error;

  bool serve_ok = false;
  std::string serve_error;
  std::thread server_thread([&] {
    serve_ok = server.ServeOne(2000, &serve_error);
  });
  ZenzUnixSocketClient client(zenz::GetZenzUnixSocketPath());
  const ZenzLiveResponse response =
      client.Convert(SyntheticRequest(121, "linux xdg synthetic prompt"));
  server_thread.join();

  EXPECT_TRUE(serve_ok) << serve_error;
  EXPECT_TRUE(client.IsAvailable());
  EXPECT_EQ(backend_calls.load(), 1);
  EXPECT_TRUE(response.ok) << response.debug;
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.generation, 121);
  EXPECT_EQ(response.value, "mock:linux xdg synthetic prompt");
}

TEST(ZenzPosixServerClientIntegrationTest, LinuxDifferentXdgRootsStayIsolated) {
  ScopedSyntheticRuntimeRoot server_root;
  ScopedSyntheticRuntimeRoot client_root;
  ASSERT_FALSE(server_root.path().empty());
  ASSERT_FALSE(client_root.path().empty());
  ASSERT_NE(server_root.path(), client_root.path());

  ScopedXdgRuntimeDir server_environment(server_root.path().c_str());
  ASSERT_TRUE(server_environment.ok());
  const std::string server_path = zenz::GetZenzUnixSocketPath();
  ASSERT_FALSE(server_path.empty());
  std::atomic<int> backend_calls{0};
  zenz_scorer::PosixZenzScorerServer server(
      server_path, [&](const zenz_scorer::PosixZenzRequest&) {
        ++backend_calls;
        return zenz_scorer::PosixZenzResponse{};
      });
  std::string error;
  ASSERT_TRUE(server.Start(&error)) << error;

  {
    ScopedXdgRuntimeDir client_environment(client_root.path().c_str());
    ASSERT_TRUE(client_environment.ok());
    const std::string client_path = zenz::GetZenzUnixSocketPath();
    ASSERT_NE(client_path, server_path);
    ZenzUnixSocketClient client(client_path);
    ZenzLiveRequest request = SyntheticRequest(122, "isolated");
    request.timeout_msec = 250;
    const ZenzLiveResponse response = client.Convert(request);
    EXPECT_FALSE(response.ok);
    EXPECT_FALSE(response.timeout);
    EXPECT_EQ(response.debug, "socket_connect_failed");
    EXPECT_EQ(backend_calls.load(), 0);
  }
  EXPECT_EQ(zenz::GetZenzUnixSocketPath(), server_path);
}

TEST(ZenzPosixServerClientIntegrationTest, LinuxMissingXdgRootFailsClosed) {
  ScopedXdgRuntimeDir environment(nullptr);
  ASSERT_TRUE(environment.ok());
  EXPECT_TRUE(zenz::GetZenzUnixSocketDirectory().empty());
  EXPECT_TRUE(zenz::GetZenzUnixSocketPath().empty());

  ZenzUnixSocketClient client(zenz::GetZenzUnixSocketPath());
  EXPECT_FALSE(client.IsAvailable());
  const ZenzLiveResponse response =
      client.Convert(SyntheticRequest(123, "missing xdg"));
  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.debug, "invalid_socket_path");
}
#endif  // __linux__

}  // namespace
}  // namespace session
}  // namespace mozc
