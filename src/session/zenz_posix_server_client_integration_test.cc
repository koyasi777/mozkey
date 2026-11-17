#include "session/zenz_client_factory.h"
#include "session/zenz_unix_socket_client.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <string>
#include <thread>
#include <utility>

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

// Cancelling the active IME request must close its Unix connection, allowing
// the serial POSIX scorer to handle the latest input. Uses a synthetic backend
// that waits for peer closure, NOT the llama model or an installed service.
TEST(ZenzPosixServerClientIntegrationTest,
     SupersedingRequestUnblocksSerialScorerWithoutRealModel) {
  ScopedTestEndpoint endpoint;
  ASSERT_FALSE(endpoint.directory().empty());

  std::mutex mu;
  std::condition_variable cv;
  bool first_entered = false;
  bool first_peer_closed = false;
  std::atomic<int> backend_calls{0};
  zenz_scorer::PosixZenzScorerServer server(
      endpoint.socket_path(), [&](const zenz_scorer::PosixZenzRequest& request) {
        ++backend_calls;
        zenz_scorer::PosixZenzResponse response;
        if (request.generation == 401) {
          {
            std::lock_guard<std::mutex> lock(mu);
            first_entered = true;
          }
          cv.notify_all();
          pollfd descriptor = {};
          descriptor.fd = request.peer_fd;
          descriptor.events = POLLIN | POLLHUP;
          const int poll_result = ::poll(&descriptor, 1, 20000);
          bool closed = poll_result > 0 &&
                        (descriptor.revents & (POLLERR | POLLHUP)) != 0;
          if (!closed && poll_result > 0 &&
              (descriptor.revents & POLLIN) != 0) {
            char byte = 0;
            closed = ::recv(request.peer_fd, &byte, 1,
                            MSG_PEEK) == 0;
          }
          {
            std::lock_guard<std::mutex> lock(mu);
            first_peer_closed = closed;
          }
          response.status = zenz::kZenzWireStatusError;
          response.debug = "test_cancelled_or_timed_out";
          return response;
        }
        response.status = zenz::kZenzWireStatusOk;
        response.value = "newer-request";
        return response;
      });
  std::string error;
  ASSERT_TRUE(server.Start(&error)) << error;

  bool serve_ok = true;
  std::thread server_thread([&] {
    for (int i = 0; i < 2; ++i) {
      std::string local_error;
      if (!server.ServeOne(5000, &local_error)) {
        serve_ok = false;
        break;
      }
    }
  });

  auto client = std::make_unique<ZenzUnixSocketClient>(endpoint.socket_path());
  ZenzLiveCorrector corrector(std::move(client));
  ZenzLiveRequest first_request =
      SyntheticRequest(401, "first synthetic prompt");
  first_request.timeout_msec = 20000;
  corrector.Submit(std::move(first_request));
  bool backend_started = false;
  {
    std::unique_lock<std::mutex> lock(mu);
    backend_started = cv.wait_for(lock, std::chrono::seconds(5), [&] {
      return first_entered;
    });
  }
  if (backend_started) {
    corrector.Submit(SyntheticRequest(402, "second synthetic prompt"));
  }
  std::optional<ZenzLiveResponse> second;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (backend_started && std::chrono::steady_clock::now() < deadline) {
    second = corrector.TakeResult(402);
    if (second.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  corrector.Stop();
  server_thread.join();

  EXPECT_TRUE(backend_started);
  EXPECT_TRUE(serve_ok);
  EXPECT_EQ(backend_calls.load(), 2);
  {
    std::lock_guard<std::mutex> lock(mu);
    EXPECT_TRUE(first_peer_closed);
  }
  ASSERT_TRUE(second.has_value());
  EXPECT_TRUE(second->ok) << second->debug;
  EXPECT_EQ(second->generation, 402);
  EXPECT_EQ(second->value, "newer-request");
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

#if defined(__linux__) && !defined(__ANDROID__)
// The production factory must reach the same endpoint as the scorer, rather
// than returning the Windows named-pipe fallback on desktop Linux.
TEST(ZenzPosixServerClientIntegrationTest, LinuxFactoryConnectsToRunningScorer) {
  ScopedSyntheticRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  ScopedXdgRuntimeDir environment(root.path().c_str());
  ASSERT_TRUE(environment.ok());
  const std::string socket_path = zenz::GetZenzUnixSocketPath();
  ASSERT_FALSE(socket_path.empty());

  zenz_scorer::PosixZenzScorerServer server(
      socket_path, [&](const zenz_scorer::PosixZenzRequest& request) {
        zenz_scorer::PosixZenzResponse response;
        response.status = zenz::kZenzWireStatusOk;
        response.value = "factory:" + request.prompt;
        return response;
      });
  std::string error;
  ASSERT_TRUE(server.Start(&error)) << error;

  std::unique_ptr<ZenzClient> client = CreateZenzClient();
  ASSERT_NE(client, nullptr);
  ASSERT_TRUE(client->IsAvailable());
  bool served = false;
  std::string serve_error;
  std::thread worker([&] { served = server.ServeOne(2000, &serve_error); });
  const ZenzLiveResponse response =
      client->Convert(SyntheticRequest(131, "production factory test"));
  worker.join();

  EXPECT_TRUE(served) << serve_error;
  EXPECT_TRUE(response.ok) << response.debug;
  EXPECT_EQ(response.generation, 131);
  EXPECT_EQ(response.value, "factory:production factory test");
}

TEST(ZenzPosixServerClientIntegrationTest, LinuxFactoryMissingXdgFailsClosed) {
  ScopedXdgRuntimeDir environment(nullptr);
  ASSERT_TRUE(environment.ok());
  std::unique_ptr<ZenzClient> client = CreateZenzClient();
  ASSERT_NE(client, nullptr);
  EXPECT_FALSE(client->IsAvailable());
  const ZenzLiveResponse response =
      client->Convert(SyntheticRequest(132, "missing xdg"));
  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.debug, "invalid_socket_path");
}

TEST(ZenzPosixServerClientIntegrationTest,
     LinuxFactoryMissingIsolatedLauncherFailsClosed) {
  ScopedSyntheticRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  ScopedXdgRuntimeDir environment(root.path().c_str());
  ASSERT_TRUE(environment.ok());
  // NEVER call the production factory with an absent socket: on a future
  // installed system it may legitimately launch the real scorer. Use an
  // explicit missing launcher under this test-owned private runtime instead.
  const std::string socket_path = zenz::GetZenzUnixSocketPath();
  ASSERT_FALSE(socket_path.empty());
  std::unique_ptr<ZenzClient> client =
      internal::CreateLinuxZenzClientForTest(
          socket_path, root.path() + "/ZenzRuntime/mozc_zenz_launcher");
  ASSERT_NE(client, nullptr);
  EXPECT_TRUE(client->IsAvailable());
  const ZenzLiveResponse response =
      client->Convert(SyntheticRequest(133, "scorer absent"));
  EXPECT_FALSE(response.ok);
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.debug, "scorer_launch_failed");
  // No socket directory, lock, scorer, model, or installed service touched.
  EXPECT_EQ(::access((root.path() + "/mozkey-zenz").c_str(), F_OK), -1);
}
// Opt-in real-runtime bridge.  Ordinary Bazel tests never start a GGUF model.
// The external harness starts a pinned scorer in a private XDG runtime root,
// waits until it is ready, and reaps the entire process family afterwards.
TEST(ZenzPosixServerClientIntegrationTest, LinuxFactoryRealGgufOptIn) {
  const char* enabled = ::getenv("MOZKEY_Z3_REAL_SCORER_TEST");
  if (enabled == nullptr || std::string(enabled) != "1") {
    GTEST_SKIP() << "Real model probe must be explicitly enabled";
  }

  const std::string socket_path = zenz::GetZenzUnixSocketPath();
  ASSERT_FALSE(socket_path.empty());
  ASSERT_EQ(::access(socket_path.c_str(), F_OK), 0);
  std::unique_ptr<ZenzClient> client = CreateZenzClient();
  ASSERT_NE(client, nullptr);
  ASSERT_TRUE(client->IsAvailable());

  ZenzLiveRequest request =
      SyntheticRequest(151, "\uEE02\uEE00キョウハイイテンキデス\uEE01");
  request.key = "きょうはいいてんきです";
  request.timeout_msec = 30000;
  request.max_output_chars = 96;
  const ZenzLiveResponse response = client->Convert(request);
  ASSERT_TRUE(response.ok) << response.debug;
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.generation, 151);
  EXPECT_EQ(response.key, request.key);
  EXPECT_EQ(response.value, "今日はいい天気です");
  std::cout << "FACTORY_REAL_MODEL_OUTPUT=" << response.value << '\n';
}

// Opt-in-only Stage4f proof: the client is constructed with the same Linux
// factory callback as production, but its executable path is injected under a
// test-owned ZenzRuntime.  Ordinary Bazel runs must SKIP this case.
// No scorer is started by the external controller; the Unix socket must be
// absent at entry, so successful conversion proves the launcher was invoked.
TEST(ZenzPosixServerClientIntegrationTest,
     LinuxFactoryAutolaunchRealGgufOptIn) {
  const char* enabled = ::getenv("MOZKEY_Z3_AUTOLAUNCH_REAL_TEST");
  if (enabled == nullptr || std::string(enabled) != "1") {
    GTEST_SKIP() << "Stage4f real model autolaunch requires explicit opt-in";
  }
  const char* launcher = ::getenv("MOZKEY_Z3_AUTOLAUNCH_TEST_LAUNCHER");
  ASSERT_NE(launcher, nullptr);
  const std::string socket_path = zenz::GetZenzUnixSocketPath();
  ASSERT_FALSE(socket_path.empty());
  ASSERT_EQ(::access(socket_path.c_str(), F_OK), -1)
      << "Cold start requires no pre-existing scorer socket";
  std::unique_ptr<ZenzClient> client =
      internal::CreateLinuxZenzClientForTest(socket_path, launcher);
  ASSERT_NE(client, nullptr);
  ASSERT_TRUE(client->IsAvailable());

  ZenzLiveRequest request =
      SyntheticRequest(201, "\uEE02\uEE00キョウハイイテンキデス\uEE01");
  request.key = "きょうはいいてんきです";
  request.timeout_msec = 30000;
  request.max_output_chars = 96;
  const ZenzLiveResponse response = client->Convert(request);
  ASSERT_TRUE(response.ok) << response.debug;
  EXPECT_FALSE(response.timeout);
  EXPECT_EQ(response.generation, 201);
  EXPECT_EQ(response.key, request.key);
  EXPECT_EQ(response.value, "今日はいい天気です");
  std::cout << "AUTOLAUNCH_REAL_MODEL_OUTPUT=" << response.value << std::endl;

  // The external test controller inspects the still-live scorer/guard/worker
  // chain before releasing this parent. On SIGKILL, kernel PDEATHSIG must
  // stop the scorer instead. Time-bounded stdin control prevents a hung test.
  pollfd control = {};
  control.fd = STDIN_FILENO;
  control.events = POLLIN;
  ASSERT_EQ(::poll(&control, 1, 30000), 1);
  ASSERT_NE(control.revents & POLLIN, 0);
  char release = '\0';
  ASSERT_EQ(::read(STDIN_FILENO, &release, 1), 1);
  EXPECT_EQ(release, 'R');
  // Client destruction joins the launch-manager thread and reaps its scorer.
}
#endif  // __linux__ && !__ANDROID__

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
