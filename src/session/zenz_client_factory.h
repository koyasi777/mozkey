#ifndef MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_
#define MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_

#include <memory>

namespace mozc {
namespace session {

class ZenzClient;

// Creates the platform transport used by ZenzLiveCorrector.
//
// This factory keeps Session independent from concrete IPC transports.  The
// Windows uses the existing named-pipe client. macOS and desktop Linux use a
// local Unix-domain socket client and launch the scorer on demand. Other
// platforms retain the unavailable named-pipe fallback until a platform
// transport is explicitly implemented.
std::unique_ptr<ZenzClient> CreateZenzClient();

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_
