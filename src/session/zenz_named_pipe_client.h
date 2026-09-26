#ifndef MOZC_SESSION_ZENZ_NAMED_PIPE_CLIENT_H_
#define MOZC_SESSION_ZENZ_NAMED_PIPE_CLIENT_H_

#include <cstdint>

#include "session/zenz_live_corrector.h"

namespace mozc {
namespace session {

class ZenzNamedPipeClient final : public ZenzClient {
 public:
  static constexpr uint32_t kDefaultTransportSafetyTimeoutMsec = 3500;

  // transport_safety_timeout_msec is an IPC failsafe, not the model inference
  // timeout carried in ZenzLiveRequest.  Current Session presentation/polling
  // gives up at about 3 seconds; the transport gets only a small cleanup margin
  // and can still be interrupted immediately by Stop().
  explicit ZenzNamedPipeClient(
      uint32_t transport_safety_timeout_msec =
          kDefaultTransportSafetyTimeoutMsec);
  ~ZenzNamedPipeClient() override = default;

  bool IsAvailable() const override;
  ZenzLiveResponse Convert(const ZenzLiveRequest& request) override;

 private:
  uint32_t transport_safety_timeout_msec_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_NAMED_PIPE_CLIENT_H_
