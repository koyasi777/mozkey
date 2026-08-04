#include "session/zenz_client_factory.h"

#include <memory>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif  // __APPLE__

#include "session/zenz_live_corrector.h"
#include "session/zenz_named_pipe_client.h"
#include "session/zenz_unix_socket_client.h"
#include "zenz/zenz_unix_socket_path.h"

namespace mozc {
namespace session {

std::unique_ptr<ZenzClient> CreateZenzClient() {
#if defined(__APPLE__) && TARGET_OS_OSX
  return std::make_unique<ZenzUnixSocketClient>(
      ::mozc::zenz::GetZenzUnixSocketPath());
#else
  return std::make_unique<ZenzNamedPipeClient>();
#endif  // __APPLE__ && TARGET_OS_OSX
}

}  // namespace session
}  // namespace mozc
