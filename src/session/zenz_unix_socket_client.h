#ifndef MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_
#define MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_

#include <functional>
#include <string>

#include "absl/time/time.h"
#include "session/zenz_live_corrector.h"

namespace mozc {
namespace session {

// Zenz transport over a local Unix-domain stream socket.
//
// The endpoint path is injected so the transport can be tested independently
// from scorer discovery and process launch. Production selection remains in
// zenz_client_factory.cc. When a scorer launcher is supplied, the client calls
// it only after an absent or stale socket indicates a cold start.
class ZenzUnixSocketClient final : public ZenzClient {
 public:
  // Returns true when the caller should wait for the scorer socket. This covers
  // both a newly spawned process and a launch already in progress. Returns
  // false only when launch definitively failed.
  using ScorerLauncher = std::function<bool()>;

  explicit ZenzUnixSocketClient(std::string socket_path);
  ZenzUnixSocketClient(std::string socket_path,
                       ScorerLauncher scorer_launcher);
  ZenzUnixSocketClient(std::string socket_path,
                       ScorerLauncher scorer_launcher,
                       absl::Duration startup_timeout);
  ~ZenzUnixSocketClient() override = default;

  bool IsAvailable() const override;
  ZenzLiveResponse Convert(const ZenzLiveRequest& request) override;

 private:
  const std::string socket_path_;
  const ScorerLauncher scorer_launcher_;
  const absl::Duration startup_timeout_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_UNIX_SOCKET_CLIENT_H_
