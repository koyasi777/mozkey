#include "session/zenz_client_factory.h"

#include <memory>

#include "session/zenz_live_corrector.h"
#include "session/zenz_named_pipe_client.h"

namespace mozc {
namespace session {

std::unique_ptr<ZenzClient> CreateZenzClient() {
  return std::make_unique<ZenzNamedPipeClient>();
}

}  // namespace session
}  // namespace mozc
