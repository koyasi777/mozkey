#ifndef MOZC_ZENZ_ZENZ_UNIX_SOCKET_PATH_H_
#define MOZC_ZENZ_ZENZ_UNIX_SOCKET_PATH_H_

#include <string>

#include "absl/strings/string_view.h"

namespace mozc {
namespace zenz {

// Returns the per-user directory reserved for the local Zenz scorer socket.
// The scorer is responsible for creating and validating this directory with
// user-only permissions before binding the socket.
std::string GetZenzUnixSocketDirectory();

// Returns the deterministic Unix-domain socket path shared by mozc_server and
// mozc_zenz_scorer. Returns an empty string on unsupported platforms or when
// the generated path cannot fit in sockaddr_un::sun_path.
std::string GetZenzUnixSocketPath();

// Returns true when |socket_path| can be represented by the native Unix-domain
// socket address structure without truncation.
bool IsValidZenzUnixSocketPath(absl::string_view socket_path);

}  // namespace zenz
}  // namespace mozc

#endif  // MOZC_ZENZ_ZENZ_UNIX_SOCKET_PATH_H_
