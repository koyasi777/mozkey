#include "zenz/zenz_unix_socket_path.h"

#include <cstdint>
#include <string>

#include "absl/strings/str_cat.h"

#if !defined(_WIN32)
#include <sys/un.h>
#include <unistd.h>
#endif  // !_WIN32

namespace mozc {
namespace zenz {

std::string GetZenzUnixSocketDirectory() {
#if defined(_WIN32)
  return "";
#else
  return absl::StrCat("/tmp/mozc_zenz_",
                      static_cast<uint64_t>(::geteuid()));
#endif  // _WIN32
}

std::string GetZenzUnixSocketPath() {
#if defined(_WIN32)
  return "";
#else
  const std::string socket_path =
      absl::StrCat(GetZenzUnixSocketDirectory(), "/scorer.sock");
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
