#ifndef MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_
#define MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_

#include <memory>

#include "session/zenz_live_corrector.h"

namespace mozc {
namespace session {

std::unique_ptr<ZenzClient> CreateDefaultZenzClient();

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_