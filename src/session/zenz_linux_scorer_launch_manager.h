#ifndef MOZC_SESSION_ZENZ_LINUX_SCORER_LAUNCH_MANAGER_H_
#define MOZC_SESSION_ZENZ_LINUX_SCORER_LAUNCH_MANAGER_H_

#if !defined(__linux__) || defined(__ANDROID__)
#error "Desktop Linux only"
#endif

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace mozc::session {

// Owns the *spawning thread* for the whole lifetime of the manager.
// Linux PR_SET_PDEATHSIG follows the creating thread, not just its process.
// Therefore all posix_spawn and waitpid operations belong to owner_thread_.
// No process is spawned by the constructor. For Stage4d this type is not yet
// wired to the production factory and never installs a runtime.
class ZenzLinuxScorerLaunchManager final {
 public:
  explicit ZenzLinuxScorerLaunchManager(std::string launcher_path);
  ~ZenzLinuxScorerLaunchManager();
  ZenzLinuxScorerLaunchManager(const ZenzLinuxScorerLaunchManager&) = delete;
  ZenzLinuxScorerLaunchManager& operator=(
      const ZenzLinuxScorerLaunchManager&) = delete;

  // Concurrent calls coalesce. Success means a child was spawned or an owned
  // child has not yet exited; it is NOT a readiness guarantee. The client
  // must still wait for an authenticated Unix socket connection.
  // Abandon waiting promptly when the IME supersedes a request. An already
  // running owner-thread spawn is NOT killed: it may serve another client.
  bool RequestLaunch(const std::function<bool()>& is_cancelled = {});

 private:
  void OwnerLoop();
  bool SpawnOwnedChild(int* child_pid) const;

  const std::string launcher_path_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread owner_thread_;
  bool thread_available_ = false;
  bool stop_ = false;
  struct RequestState {
    bool done = false;
    bool success = false;
  };
  std::shared_ptr<RequestState> pending_;
  std::shared_ptr<RequestState> running_;
};

}  // namespace mozc::session

#endif  // MOZC_SESSION_ZENZ_LINUX_SCORER_LAUNCH_MANAGER_H_
