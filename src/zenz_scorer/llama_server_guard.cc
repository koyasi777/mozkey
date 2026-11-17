// Dedicated owner of a single llama-server child.  The scorer is the only
// writer of the lifetime pipe; EOF also works when the scorer is SIGKILLed.
#if !defined(__APPLE__) && !(defined(__linux__) && !defined(__ANDROID__))
#error "llama_server_guard requires macOS or desktop Linux"
#endif

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <vector>

#include "zenz_scorer/llama_server_guard_protocol.h"

#include <poll.h>
#include <sys/socket.h>
#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/mman.h>
#endif
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

constexpr int kLifetimeFd = mozc::zenz_scorer::kGuardLifetimeFd;
constexpr int kStartupFd = mozc::zenz_scorer::kGuardStartupFd;
constexpr int kApiKeyFd = mozc::zenz_scorer::kGuardApiKeyFd;
constexpr int kWorkerScorerFd = mozc::zenz_scorer::kWorkerScorerLifetimeFd;
constexpr int kWorkerGuardFd = mozc::zenz_scorer::kWorkerGuardLifetimeFd;
#if defined(__linux__) && !defined(__ANDROID__)
constexpr int kWorkerKeyFd = mozc::zenz_scorer::kWorkerApiKeyFd;
constexpr char kWorkerKeyPath[] = "/proc/self/fd/25";

// Keep the key outside argv, process environment, and filesystem paths. The
// pinned llama-server v0.7.0 supports --api-key-file. A sealed memfd is used
// so the worker can read the exact key without a named or writable file.
int CreateSealedKeyFile(std::string_view key) {
  int fd = ::memfd_create("mozkey-zenz-auth", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) return -1;
  const int raised = ::fcntl(fd, F_DUPFD_CLOEXEC, 64);
  ::close(fd);
  if (raised < 0) return -1;
  fd = raised;
  auto write_all = [&](std::string_view value) {
    size_t offset = 0;
    while (offset < value.size()) {
      const ssize_t n = ::write(fd, value.data() + offset,
                                value.size() - offset);
      if (n > 0) {
        offset += static_cast<size_t>(n);
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else {
        return false;
      }
    }
    return true;
  };
  if (!write_all(key) || !write_all("\n") || ::lseek(fd, 0, SEEK_SET) != 0 ||
      ::fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW |
                                    F_SEAL_SEAL) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}
#endif

constexpr int kPollIntervalMsec = 50;
constexpr int kGracePollCount = 40;  // 2 seconds before SIGKILL.

volatile std::sig_atomic_t g_stop_requested = 0;
void OnStop(int) { g_stop_requested = 1; }

void ReportStartup(bool ok, pid_t worker_pid = -1) {
  mozc::zenz_scorer::GuardStartupMessage message;
  message.ok = ok ? 1 : 0;
  message.worker_pid = worker_pid;
  const char* ptr = reinterpret_cast<const char*>(&message);
  size_t remaining = sizeof(message);
  while (remaining > 0) {
    const ssize_t n = ::write(kStartupFd, ptr, remaining);
    if (n > 0) {
      ptr += n;
      remaining -= static_cast<size_t>(n);
    } else if (n < 0 && errno != EINTR) {
      break;
    }
  }
  ::close(kStartupFd);
}

// Only the scorer's socket endpoint can supply the 64-character ephemeral key.
// A finite timeout and bounded buffer prohibit hanging or unbounded input.
bool ReadApiKey(std::string* key) {
  key->clear();
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (key->size() < mozc::zenz_scorer::kGuardApiKeyLength &&
         !g_stop_requested) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) {
      break;
    }
    pollfd descriptor = {};
    descriptor.fd = kApiKeyFd;
    descriptor.events = POLLIN;
    const int ready = ::poll(&descriptor, 1, static_cast<int>(remaining));
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      break;
    }
    char chunk[mozc::zenz_scorer::kGuardApiKeyLength];
    const ssize_t n = ::recv(kApiKeyFd, chunk,
        mozc::zenz_scorer::kGuardApiKeyLength - key->size(), 0);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      break;
    }
    key->append(chunk, static_cast<size_t>(n));
  }
  ::close(kApiKeyFd);
  if (key->size() != mozc::zenz_scorer::kGuardApiKeyLength) {
    return false;
  }
  for (char c : *key) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool HasExited(pid_t pid, int* status) {
  pid_t result;
  do {
    result = ::waitpid(pid, status, WNOHANG);
  } while (result < 0 && errno == EINTR);
  return result == pid || (result < 0 && errno == ECHILD);
}

// Only the direct child's still-unreaped process group is signalled. Never
// probe or kill an arbitrary PID discovered by name or from a pid file.
void StopChild(pid_t child_pid, int* status) {
  if (child_pid <= 0 || HasExited(child_pid, status)) {
    return;
  }
  if (::kill(-child_pid, SIGTERM) != 0 && errno != ESRCH) {
    ::kill(child_pid, SIGTERM);
  }
  for (int i = 0; i < kGracePollCount; ++i) {
    if (HasExited(child_pid, status)) {
      return;
    }
    ::usleep(kPollIntervalMsec * 1000);
  }
  if (::kill(-child_pid, SIGKILL) != 0 && errno != ESRCH) {
    ::kill(child_pid, SIGKILL);
  }
  pid_t result;
  do {
    result = ::waitpid(child_pid, status, 0);
  } while (result < 0 && errno == EINTR);
}

int MirrorWorkerExit(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    const int signum = WTERMSIG(status);
    ::signal(signum, SIG_DFL);
    ::raise(signum);
    return 128 + signum;
  }
  return 70;
}

// Move both ends out of the reserved descriptor range before adding spawn
// mappings. Both are CLOEXEC by default, and the worker explicitly receives
// only the read end.  Do not allow the worker to inherit the guard's writer.
bool CreateGuardLifetimePipe(int pipe_fds[2]) {
  int original[2] = {-1, -1};
  if (::pipe(original) != 0) {
    return false;
  }
  const int reader = ::fcntl(original[0], F_DUPFD_CLOEXEC, 64);
  const int writer = ::fcntl(original[1], F_DUPFD_CLOEXEC, 64);
  ::close(original[0]);
  ::close(original[1]);
  if (reader < 0 || writer < 0) {
    if (reader >= 0) ::close(reader);
    if (writer >= 0) ::close(writer);
    return false;
  }
  pipe_fds[0] = reader;
  pipe_fds[1] = writer;
  return true;
}

int RunGuard(const char* const* argv, int api_key_fd) {
#if defined(__APPLE__)
  static_cast<void>(api_key_fd);
#endif
  // The scorer's existing lifetime pipe is read by both guard (FD20) and
  // worker (FD23).  Only the scorer owns its write end.  A *separate* pipe
  // held open by guard supplies worker FD24.  Thus guard SIGKILL closes FD24's
  // writer and worker can exit even while the scorer is still alive.
  int guard_pipe[2] = {-1, -1};
  if (!CreateGuardLifetimePipe(guard_pipe)) {
    ReportStartup(false);
    return 71;
  }

  posix_spawn_file_actions_t actions;
  int rc = ::posix_spawn_file_actions_init(&actions);
  bool actions_initialized = rc == 0;
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_adddup2(
        &actions, kLifetimeFd, kWorkerScorerFd);
  }
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_adddup2(
        &actions, guard_pipe[0], kWorkerGuardFd);
  }
#if defined(__linux__) && !defined(__ANDROID__)
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_adddup2(
        &actions, api_key_fd, kWorkerKeyFd);
  }
#endif
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_addclose(&actions, kLifetimeFd);
  }
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_addclose(&actions, kStartupFd);
  }
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_addclose(&actions, guard_pipe[0]);
  }
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_addclose(&actions, guard_pipe[1]);
  }
#if defined(__linux__) && !defined(__ANDROID__)
  // FD23/24 are the worker's only inherited lifetime channels. After
  // duplicating their sources, close every unrelated descriptor, including
  // a future descriptor opened by another thread during spawn.
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
  if (rc == 0) {
    rc = ::posix_spawn_file_actions_addclosefrom_np(
        &actions, kWorkerKeyFd + 1);
  }
#else
  if (rc == 0) rc = ENOTSUP;
#endif
#else
  if (rc == 0) rc = ENOTSUP;
#endif
  for (int fd = STDERR_FILENO + 1; fd < kWorkerScorerFd && rc == 0;
       ++fd) {
    if (fd != kLifetimeFd && fd != kStartupFd) {
      rc = ::posix_spawn_file_actions_addclose(&actions, fd);
    }
  }
#endif

  posix_spawnattr_t attributes;
  bool attr_initialized = false;
  if (rc == 0) {
    rc = ::posix_spawnattr_init(&attributes);
    attr_initialized = rc == 0;
  }
  if (rc == 0) {
    rc = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  }
  if (rc == 0) {
    rc = ::posix_spawnattr_setpgroup(&attributes, 0);
  }

  // The option is never accepted by unmodified llama-server.  Therefore
  // guard and pinned llama.cpp MUST be upgraded as one atomic runtime pair.
  std::vector<const char*> worker_argv;
  worker_argv.push_back(argv[0]);
  worker_argv.push_back(mozc::zenz_scorer::kWorkerDualLifetimeSwitch);
  for (size_t i = 1; argv[i] != nullptr; ++i) {
    worker_argv.push_back(argv[i]);
  }
  worker_argv.push_back(nullptr);

  // Ignore inherited external API credentials: llama.cpp appends keys from
  // LLAMA_API_KEY and LLAMA_ARG_API_KEY_FILE rather than replacing them.
  // The one key supplied by Mozkey must be the only accepted credential.
#if defined(__linux__) && !defined(__ANDROID__)
  std::vector<char*> worker_environment;
  for (char** item = environ; *item != nullptr; ++item) {
    const std::string_view variable(*item);
    if (variable.starts_with("LLAMA_API_KEY=") ||
        variable.starts_with("LLAMA_ARG_API_KEY_FILE=")) {
      continue;
    }
    worker_environment.push_back(*item);
  }
  worker_environment.push_back(nullptr);
#endif
  pid_t child_pid = -1;
  if (rc == 0) {
    rc = ::posix_spawn(&child_pid, argv[0], &actions, &attributes,
                       const_cast<char* const*>(worker_argv.data()),
#if defined(__linux__) && !defined(__ANDROID__)
                       worker_environment.data());
#else
                       environ);
#endif
  }
#if defined(__linux__) && !defined(__ANDROID__)
  ::close(api_key_fd);
#endif
  if (attr_initialized) {
    ::posix_spawnattr_destroy(&attributes);
  }
  if (actions_initialized) {
    ::posix_spawn_file_actions_destroy(&actions);
  }
  ::close(guard_pipe[0]);
  if (rc != 0 || child_pid <= 0) {
    ::close(guard_pipe[1]);
    ReportStartup(false);
    return 72;
  }
  ReportStartup(true, child_pid);

  int status = 0;
  while (!g_stop_requested) {
    if (HasExited(child_pid, &status)) {
      ::close(guard_pipe[1]);
      return MirrorWorkerExit(status);
    }
    pollfd descriptor = {};
    descriptor.fd = kLifetimeFd;
    descriptor.events = POLLIN | POLLHUP;
    int result = ::poll(&descriptor, 1, kPollIntervalMsec);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result > 0 || (result < 0 && errno != EINTR)) {
      break;
    }
  }
  StopChild(child_pid, &status);
  ::close(guard_pipe[1]);
  ::close(kLifetimeFd);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || std::strcmp(argv[1], "--") != 0) {
    return 64;
  }
  // SIGPIPE must not prevent child cleanup on diagnostic output failures.
  ::signal(SIGPIPE, SIG_IGN);
  ::signal(SIGTERM, OnStop);
  ::signal(SIGINT, OnStop);
  // Reject duplicate or malformed API key arguments and require the one
  // placeholder generated by the scorer.  No secret is stored in guard argv.
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<size_t>(argc - 2));
  int key_index = -1;
  for (int i = 2; i < argc; ++i) {
    arguments.emplace_back(argv[i]);
    if (arguments.back().starts_with("--api-key=")) {
      ReportStartup(false);
      return 64;
    }
    if (arguments.back() == "--api-key") {
      if (key_index != -1 || i + 1 >= argc ||
          std::strcmp(argv[i + 1],
              mozc::zenz_scorer::kGuardApiKeyPlaceholder) != 0) {
        ReportStartup(false);
        return 64;
      }
      key_index = i - 2;
    }
  }
  if (key_index < 0) {
    ReportStartup(false);
    return 64;
  }
  std::string key;
  if (!ReadApiKey(&key)) {
    ReportStartup(false);
    return 65;
  }
#if defined(__linux__) && !defined(__ANDROID__)
  const int key_fd = CreateSealedKeyFile(key);
  std::fill(key.begin(), key.end(), '\0');
  key.clear();
  if (key_fd < 0) {
    ReportStartup(false);
    return 66;
  }
  arguments[static_cast<size_t>(key_index)] = "--api-key-file";
  arguments[static_cast<size_t>(key_index + 1)] = kWorkerKeyPath;
#else
  arguments[static_cast<size_t>(key_index + 1)] = std::move(key);
  const int key_fd = -1;
#endif
  std::vector<const char*> worker_argv;
  worker_argv.reserve(arguments.size() + 1);
  for (std::string& argument : arguments) {
    worker_argv.push_back(argument.c_str());
  }
  worker_argv.push_back(nullptr);
  return RunGuard(worker_argv.data(), key_fd);
}
