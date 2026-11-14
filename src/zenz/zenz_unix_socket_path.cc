#include "zenz/zenz_unix_socket_path.h"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "absl/strings/str_cat.h"

#if !defined(_WIN32)
#include <sys/un.h>
#include <unistd.h>
#endif  // !_WIN32

#if defined(__linux__)
#include <sys/stat.h>
#endif  // __linux__

namespace mozc {
namespace zenz {
namespace {

#if defined(__linux__)
// Reject dot components as well as repeated separators.  The runtime root
// is supplied by the login environment, never constructed from input text.
bool HasUnsafePathComponents(const std::string& path) {
  if (path.size() < 2 || path[0] != '/') {
    return true;
  }
  size_t begin = 1;
  while (begin < path.size()) {
    const size_t end = path.find('/', begin);
    const size_t length = (end == std::string::npos ? path.size() : end) - begin;
    const std::string component = path.substr(begin, length);
    if (component.empty() || component == "." || component == "..") {
      return true;
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return false;
}

std::string GetValidatedRuntimeRoot() {
  const char* const value = std::getenv("XDG_RUNTIME_DIR");
  if (value == nullptr || value[0] == '\0') {
    // Zenz is optional: refuse an endpoint rather than silently selecting a
    // persistent /tmp fallback.  A secure fallback can be added separately.
    return "";
  }
  std::string path(value);
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  if (HasUnsafePathComponents(path)) {
    return "";
  }
  struct stat status = {};
  if (::lstat(path.c_str(), &status) != 0 ||
      !S_ISDIR(status.st_mode) || status.st_uid != ::geteuid() ||
      (status.st_mode & 07777) != 0700) {
    return "";
  }
  return path;
}
#endif  // __linux__

}  // namespace

std::string GetZenzUnixSocketDirectory() {
#if defined(_WIN32)
  return "";
#elif defined(__linux__)
  const std::string root = GetValidatedRuntimeRoot();
  return root.empty() ? std::string() : absl::StrCat(root, "/mozkey-zenz");
#else
  return absl::StrCat("/tmp/mozc_zenz_",
                      static_cast<uint64_t>(::geteuid()));
#endif
}

std::string GetZenzUnixSocketPath() {
#if defined(_WIN32)
  return "";
#else
  const std::string directory = GetZenzUnixSocketDirectory();
  if (directory.empty()) {
    return "";
  }
  const std::string socket_path = absl::StrCat(directory, "/scorer.sock");
  return IsValidZenzUnixSocketPath(socket_path) ? socket_path : std::string();
#endif  // _WIN32
}

bool IsValidZenzUnixSocketPath(const absl::string_view socket_path) {
#if defined(_WIN32)
  return false;
#else
  return !socket_path.empty() &&
         socket_path.find('\0') == absl::string_view::npos &&
         socket_path.size() < sizeof(sockaddr_un::sun_path);
#endif  // _WIN32
}

}  // namespace zenz
}  // namespace mozc
