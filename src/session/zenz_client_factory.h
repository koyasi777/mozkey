#ifndef MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_
#define MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_

#include <memory>

namespace mozc {
namespace session {

class ZenzClient;

// Creates the platform transport used by ZenzLiveCorrector.
//
// This factory keeps Session independent from concrete IPC transports.  The
// current implementation preserves the existing named-pipe client on every
// platform; a macOS Unix-domain-socket client can be selected here without
// changing Session again.
std::unique_ptr<ZenzClient> CreateZenzClient();

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_
