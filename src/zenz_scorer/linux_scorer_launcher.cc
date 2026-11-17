// Experimental Linux scorer launcher for Mozkey Z3 Stage4a.
// This file is NOT yet wired into Mozkey or any Debian package.
// Run via posix_spawn(executable, argv={executable, decimal_parent_pid, nullptr}).
// Only executes ./mozc_zenz_scorer located beside /proc/self/exe.
// The launch helper becomes the scorer via execve, retaining PR_SET_PDEATHSIG.
#if !defined(__linux__)
#error "This launcher is Linux-only"
#endif

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {
constexpr const char* kSelfName = "mozc_zenz_launcher";
constexpr const char* kScorerName = "mozc_zenz_scorer";

bool ParseExpectedParent(const char* arg, pid_t* result) {
  if (arg == nullptr || *arg == '\0' || arg[0] == '-' || arg[0] == '+') {
    return false;
  }
  for (const char* p = arg; *p; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
  }
  errno = 0;
  char* end = nullptr;
  const long number = std::strtol(arg, &end, 10);
  if (errno != 0 || end == arg || *end != '\0' || number <= 1 ||
      number > INT_MAX) {
    return false;
  }
  *result = static_cast<pid_t>(number);
  return true;
}

bool IsTrustedFile(const struct stat& stat, uid_t owner) {
  return S_ISREG(stat.st_mode) && stat.st_uid == owner &&
         (stat.st_mode & (S_IWGRP | S_IWOTH | S_ISUID | S_ISGID)) == 0 &&
         (stat.st_mode & S_IXUSR) != 0;
}

bool ResolveScorer(std::string* scorer_path) {
  // /proc/self/exe resolves the actual launcher binary, not argv[0].
  char path[4096];
  const ssize_t count = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (count <= 0 || count >= static_cast<ssize_t>(sizeof(path) - 1)) {
    return false;
  }
  path[count] = '\0';
  const std::string exe(path);
  const size_t last_slash = exe.rfind('/');
  if (last_slash == std::string::npos ||
      exe.substr(last_slash + 1) != kSelfName) {
    return false;
  }
  const std::string directory = exe.substr(0, last_slash);
  struct stat dir_stat = {};
  if (::lstat(directory.c_str(), &dir_stat) != 0 ||
      !S_ISDIR(dir_stat.st_mode) ||
      (dir_stat.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
      (dir_stat.st_uid != 0 && dir_stat.st_uid != ::geteuid())) {
    return false;
  }
  struct stat self_stat = {};
  if (::stat("/proc/self/exe", &self_stat) != 0 ||
      !IsTrustedFile(self_stat, dir_stat.st_uid)) {
    return false;
  }
  *scorer_path = directory + "/" + kScorerName;
  struct stat scorer_stat = {};
  if (::lstat(scorer_path->c_str(), &scorer_stat) != 0 ||
      !IsTrustedFile(scorer_stat, dir_stat.st_uid)) {
    return false;
  }
  return true;
}

bool CloseInheritedDescriptors() {
#if defined(SYS_close_range)
  if (::syscall(SYS_close_range, 3U, UINT_MAX, 0U) == 0) {
    return true;
  }
  if (errno != ENOSYS && errno != EINVAL) {
    return false;
  }
#endif
  // Conservative fallback for kernels before close_range(2).
  // Do not close the directory stream's own FD while enumerating.
  DIR* dir = ::opendir("/proc/self/fd");
  if (dir == nullptr) {
    return false;  // Fail closed, never exec scorer with unknown FDs.
  }
  const int directory_fd = ::dirfd(dir);
  std::vector<int> close_fds;
  errno = 0;
  for (struct dirent* ent = ::readdir(dir); ent != nullptr;
       ent = ::readdir(dir)) {
    if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
      continue;
    }
    char* end = nullptr;
    const long fd = std::strtol(ent->d_name, &end, 10);
    if (*end == '\0' && fd > 2 && fd <= INT_MAX &&
        fd != directory_fd) {
      close_fds.push_back(static_cast<int>(fd));
    }
  }
  const bool complete = errno == 0;
  ::closedir(dir);
  if (!complete) {
    return false;
  }
  for (int fd : close_fds) {
    if (::close(fd) != 0 && errno != EBADF) {
      return false;
    }
  }
  return true;
}
}  // namespace

int main(int argc, char* argv[]) {
  pid_t expected_parent = -1;
  if (argc != 2 || !ParseExpectedParent(argv[1], &expected_parent)) {
    return 64;
  }
  // Install the kernel-backed parent-death trigger before any potentially
  // expensive file I/O. SIGKILL is intentional: it must not be ignorable.
  if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
    return 70;
  }
  // Catches the death race between posix_spawn and PR_SET_PDEATHSIG.
  if (::getppid() != expected_parent) {
    return 71;
  }
  std::string scorer_path;
  if (!ResolveScorer(&scorer_path)) {
    return 72;
  }
  if (!CloseInheritedDescriptors()) {
    return 73;
  }
  // No shell, no user-supplied executable path or command-line arguments.
  char* const scorer_argv[] = {
      const_cast<char*>(scorer_path.c_str()), nullptr};
  ::execve(scorer_path.c_str(), scorer_argv, environ);
  return 74;
}
