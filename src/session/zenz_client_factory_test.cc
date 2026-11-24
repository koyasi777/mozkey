#include "session/zenz_client_factory.h"

#include <memory>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <thread>
#include <vector>

#include "base/process.h"
#endif  // TARGET_OS_OSX
#endif  // __APPLE__

#include "session/zenz_live_corrector.h"
#include "testing/gunit.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include "zenz/zenz_unix_socket_path.h"
#endif

namespace mozc {
namespace session {
namespace {

TEST(ZenzClientFactoryTest, CreatesCurrentPlatformClient) {
  std::unique_ptr<ZenzClient> client = CreateZenzClient();
  ASSERT_NE(client, nullptr);

#if defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX)
  EXPECT_TRUE(client->IsAvailable());
#elif defined(__linux__) && !defined(__ANDROID__)
  // IsAvailable checks endpoint path validity, not scorer readiness.
  EXPECT_EQ(client->IsAvailable(),
            !::mozc::zenz::GetZenzUnixSocketPath().empty());
#else
  EXPECT_FALSE(client->IsAvailable());
#endif
}

#if defined(__APPLE__) && TARGET_OS_OSX

// These tests launch only test-owned child processes; they do not start the
// installed Zenz service or send signals to unrelated system processes.
class ScopedTestChild {
 public:
  explicit ScopedTestChild(pid_t pid) : pid_(pid) {}
  ScopedTestChild(const ScopedTestChild&) = delete;
  ScopedTestChild& operator=(const ScopedTestChild&) = delete;

  ~ScopedTestChild() {
    // Ownership of waitpid() passes to the asynchronous production reaper
    // immediately after it is launched. Never race that thread, even if a
    // test times out: the PID may already have been reaped and reused.
    if (reaper_started_) {
      return;
    }
    // Cleanup is allowed only before ownership was transferred.
    int status = 0;
    pid_t result = -1;
    do {
      result = ::waitpid(pid_, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
      ::kill(pid_, SIGKILL);
      while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }

  pid_t pid() const { return pid_; }
  void MarkReaperStarted() { reaper_started_ = true; }

 private:
  const pid_t pid_;
  bool reaper_started_ = false;
};

bool SpawnTestChild(const char* executable, const char* args, pid_t* pid) {
  size_t raw_pid = 0;
  if (!Process::SpawnProcess(executable, args, &raw_pid) || raw_pid == 0) {
    return false;
  }
  *pid = static_cast<pid_t>(raw_pid);
  return *pid > 0;
}

bool WaitForReapedChild(pid_t pid) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    // kill(pid, 0) does not wait for or reap a child. A zombie remains
    // addressable; ESRCH means the async reaper has removed it.
    if (::kill(pid, 0) == -1 && errno == ESRCH) {
      int status = 0;
      errno = 0;
      return ::waitpid(pid, &status, WNOHANG) == -1 && errno == ECHILD;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

TEST(ZenzClientFactoryTest, ReapsNormallyExitingChild) {
  pid_t pid = 0;
  ASSERT_TRUE(SpawnTestChild("/usr/bin/true", "", &pid));
  ScopedTestChild child(pid);
  internal::ReapScorerAsync(child.pid());
  child.MarkReaperStarted();
  EXPECT_TRUE(WaitForReapedChild(child.pid()));
}

TEST(ZenzClientFactoryTest, ReapsNonzeroExitChild) {
  pid_t pid = 0;
  ASSERT_TRUE(SpawnTestChild("/usr/bin/false", "", &pid));
  ScopedTestChild child(pid);
  internal::ReapScorerAsync(child.pid());
  child.MarkReaperStarted();
  EXPECT_TRUE(WaitForReapedChild(child.pid()));
}

TEST(ZenzClientFactoryTest, ReapsSignaledChild) {
  pid_t pid = 0;
  ASSERT_TRUE(SpawnTestChild("/bin/sleep", "30", &pid));
  ScopedTestChild child(pid);
  internal::ReapScorerAsync(child.pid());
  child.MarkReaperStarted();
  ASSERT_EQ(::kill(child.pid(), SIGKILL), 0);
  EXPECT_TRUE(WaitForReapedChild(child.pid()));
}

TEST(ZenzClientFactoryTest, ReapsRepeatedRapidExits) {
  // Exercise multiple detached waiters without serially waiting after spawn.
  std::vector<std::unique_ptr<ScopedTestChild>> children;
  for (int i = 0; i < 16; ++i) {
    pid_t pid = 0;
    ASSERT_TRUE(SpawnTestChild("/usr/bin/true", "", &pid));
    children.push_back(std::make_unique<ScopedTestChild>(pid));
    internal::ReapScorerAsync(pid);
    children.back()->MarkReaperStarted();
  }
  for (const auto& child : children) {
    ASSERT_TRUE(WaitForReapedChild(child->pid()));
  }
}

#endif  // __APPLE__ && TARGET_OS_OSX

}  // namespace
}  // namespace session
}  // namespace mozc
