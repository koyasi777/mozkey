#ifndef MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_
#define MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_

#include <string>

#include "session/zenz_live_corrector.h"

namespace mozc {
namespace session {

// Zenz transport over a local Unix-domain stream socket.
//
// The endpoint path is injected so the transport can be tested independently
// from scorer discovery and process launch.  Production selection remains in
// zenz_client_factory.cc.
class ZenzUnixSocketClient final : public ZenzClient {
 public:
  explicit ZenzUnixSocketClient(std::string socket_path);
  ~ZenzUnixSocketClient() override = default;

  bool IsAvailable() const override;
  ZenzLiveResponse Convert(const ZenzLiveRequest& request) override;

 private:
  const std::string socket_path_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_
