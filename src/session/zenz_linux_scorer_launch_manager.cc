#include "session/zenz_linux_scorer_launch_manager.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <system_error>
#include <utility>

extern char** environ;

namespace mozc::session {
namespace {

constexpr auto kLaunchCooldown = std::chrono::seconds(2);
constexpr auto kRequestWait = std::chrono::seconds(3);
constexpr auto kCancelCheckInterval = std::chrono::milliseconds(20);
constexpr auto kReapPoll = std::chrono::milliseconds(100);
constexpr auto kGracefulStop = std::chrono::milliseconds(500);
constexpr auto kForcedStop = std::chrono::milliseconds(1500);

// Only fixed-name launcher files in a trusted directory are acceptable.
// The product will pass SystemUtil::GetServerDirectory()/ZenzRuntime/...;
// tests may use isolated, user-owned 0700 directories. No PATH lookup.
bool IsTrustedLauncher(const std::string& path) {
  constexpr char kSuffix[] = "/mozc_zenz_launcher";
  constexpr size_t kSuffixSize = sizeof(kSuffix) - 1;
  if (path.size() <= kSuffixSize || path[0] != '/' ||
      path.compare(path.size() - kSuffixSize, kSuffixSize, kSuffix) != 0 ||
      path.find("/../") != std::string::npos ||
      path.find("/./") != std::string::npos ||
      path.find("//") != std::string::npos) {
    return false;
  }
  struct stat directory = {};
  struct stat executable = {};
  const auto dir = path.substr(0, path.size() - kSuffixSize);
  if (::lstat(dir.c_str(), &directory) != 0 ||
      !S_ISDIR(directory.st_mode) ||
      (directory.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
      (directory.st_uid != 0 && directory.st_uid != ::geteuid())) {
    return false;
  }
  if (::lstat(path.c_str(), &executable) != 0 ||
      !S_ISREG(executable.st_mode) ||
      executable.st_uid != directory.st_uid ||
      (executable.st_mode & (S_IWGRP | S_IWOTH | S_ISUID | S_ISGID)) != 0 ||
      (executable.st_mode & S_IXUSR) == 0) {
    return false;
  }
  return true;
}

void ReapIfExited(pid_t* child) {
  if (*child <= 0) return;
  int status = 0;
  pid_t result;
  do {
    result = ::waitpid(*child, &status, WNOHANG);
  } while (result < 0 && errno == EINTR);
  if (result == *child || (result < 0 && errno == ECHILD)) {
    *child = -1;
  }
}

// Never signal a PID that has already been reaped by this manager.
// Child PID cannot be recycled until it is reaped. This class is the only
// thread that performs waitpid on its own child.
void StopAndReap(pid_t* child) {
  ReapIfExited(child);
  if (*child <= 0) return;
  ::kill(*child, SIGTERM);
  const auto graceful_deadline =
      std::chrono::steady_clock::now() + kGracefulStop;
  while (*child > 0 && std::chrono::steady_clock::now() < graceful_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ReapIfExited(child);
  }
  if (*child <= 0) return;
  ::kill(*child, SIGKILL);
  const auto hard_deadline = std::chrono::steady_clock::now() + kForcedStop;
  while (*child > 0 && std::chrono::steady_clock::now() < hard_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ReapIfExited(child);
  }
  // Kernel PR_SET_PDEATHSIG remains an additional last-resort guarantee.
}

}  // namespace

ZenzLinuxScorerLaunchManager::ZenzLinuxScorerLaunchManager(
    std::string launcher_path)
    : launcher_path_(std::move(launcher_path)) {
  try {
    owner_thread_ = std::thread([this] { OwnerLoop(); });
    thread_available_ = true;
  } catch (const std::system_error&) {
    // Fail closed: no fallback spawning on the calling thread.
    thread_available_ = false;
  }
}

ZenzLinuxScorerLaunchManager::~ZenzLinuxScorerLaunchManager() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (owner_thread_.joinable()) owner_thread_.join();
}

bool ZenzLinuxScorerLaunchManager::RequestLaunch(
    const std::function<bool()>& is_cancelled) {
  // No process may be spawned by the requesting/inference thread.
  std::unique_lock<std::mutex> lock(mutex_);
  if (!thread_available_ || stop_ || (is_cancelled && is_cancelled())) {
    return false;
  }
  std::shared_ptr<RequestState> request;
  if (pending_) {
    request = pending_;
  } else if (running_) {
    request = running_;
  } else {
    request = std::make_shared<RequestState>();
    pending_ = request;
    cv_.notify_one();
  }

  const auto deadline = std::chrono::steady_clock::now() + kRequestWait;
  while (!stop_ && !request->done) {
    if (is_cancelled && is_cancelled()) {
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return false;
    }
    cv_.wait_until(lock, std::min(deadline, now + kCancelCheckInterval),
                   [this, &request] { return stop_ || request->done; });
  }
  return !stop_ && request->done && request->success &&
         !(is_cancelled && is_cancelled());
}

bool ZenzLinuxScorerLaunchManager::SpawnOwnedChild(int* child_pid) const {
  if (!IsTrustedLauncher(launcher_path_)) return false;
  const std::string parent = std::to_string(::getpid());
  char* const argv[] = {const_cast<char*>(launcher_path_.c_str()),
                        const_cast<char*>(parent.c_str()), nullptr};
  pid_t pid = -1;
  // This call executes only on owner_thread_. It must never be moved into
  // RequestLaunch(), a transient async inference thread, or a detached reaper.
  const int rc = ::posix_spawn(&pid, launcher_path_.c_str(), nullptr,
                              nullptr, argv, environ);
  if (rc != 0 || pid <= 1) return false;
  *child_pid = static_cast<int>(pid);
  return true;
}

void ZenzLinuxScorerLaunchManager::OwnerLoop() {
  pid_t child = -1;
  bool attempted = false;
  std::chrono::steady_clock::time_point last_attempt;
  for (;;) {
    ReapIfExited(&child);
    std::unique_lock<std::mutex> lock(mutex_);
    if (stop_) break;
    if (!pending_) {
      cv_.wait_for(lock, kReapPoll,
                   [this] { return stop_ || static_cast<bool>(pending_); });
      continue;
    }
    const auto request = pending_;
    pending_.reset();
    running_ = request;
    lock.unlock();

    bool success = child > 0;
    const auto now = std::chrono::steady_clock::now();
    if (!success && (!attempted || now - last_attempt >= kLaunchCooldown)) {
      attempted = true;
      last_attempt = now;
      int spawned = -1;
      success = SpawnOwnedChild(&spawned);
      if (success) child = static_cast<pid_t>(spawned);
    }

    lock.lock();
    request->success = success && !stop_;
    request->done = true;
    running_.reset();
    cv_.notify_all();
  }
  // No request thread can spawn or reap. The creating thread stays alive
  // until its directly owned child has been stopped and reaped.
  StopAndReap(&child);
  cv_.notify_all();
}

}  // namespace mozc::session
