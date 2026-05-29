#ifndef MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_
#define MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_

#include "session/zenz_live_corrector.h"

namespace mozc {
namespace session {

class ZenzUnixSocketClient final : public ZenzClient {
 public:
  ZenzUnixSocketClient() = default;
  ~ZenzUnixSocketClient() override = default;

  bool IsAvailable() const override;
  ZenzLiveResponse Convert(const ZenzLiveRequest& request) override;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_