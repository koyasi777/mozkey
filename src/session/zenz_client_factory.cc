#include "session/zenz_client_factory.h"

#include <memory>

#include "session/zenz_named_pipe_client.h"
#include "session/zenz_unix_socket_client.h"

namespace mozc {
namespace session {

std::unique_ptr<ZenzClient> CreateDefaultZenzClient() {
#if defined(__APPLE__)
  return std::make_unique<ZenzUnixSocketClient>();
#else
  return std::make_unique<ZenzNamedPipeClient>();
#endif
}

}  // namespace session
}  // namespace mozc