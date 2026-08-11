// Copyright 2026
// Licensed under the same terms as Mozc.

#include "session/zenz_context_request.h"

#include <algorithm>
#include <cstdint>

namespace mozc {
namespace session {
namespace {

constexpr uint32_t kDefaultPrecedingLength = 24;
constexpr uint32_t kDefaultFollowingLength = 10;

// Platform acquisition is intentionally bounded even though the historical
// Session-side left-context setting has no equivalent clamp.
constexpr uint32_t kMaxAcquisitionLength = 128;

}  // namespace

ZenzContextRequest GetZenzContextRequest(
    const config::Config& config,
    const commands::Context::InputFieldType input_field_type,
    const bool may_snapshot_client_context) {
  ZenzContextRequest request;

  if (!may_snapshot_client_context ||
      !config.use_live_conversion() ||
      !config.use_zenz_live_correction() ||
      input_field_type == commands::Context::PASSWORD) {
    return request;
  }

  const uint32_t configured_preceding_length =
      config.has_zenz_live_correction_left_context_length()
          ? config.zenz_live_correction_left_context_length()
          : kDefaultPrecedingLength;
  request.preceding_length =
      std::min(configured_preceding_length, kMaxAcquisitionLength);

  if (!config.use_zenz_live_correction_right_context()) {
    return request;
  }

  const uint32_t configured_following_length =
      config.has_zenz_live_correction_right_context_length()
          ? config.zenz_live_correction_right_context_length()
          : kDefaultFollowingLength;
  request.following_length =
      std::min(configured_following_length, kMaxAcquisitionLength);

  return request;
}

void AttachZenzContextRequest(const ZenzContextRequest& request,
                              commands::Output* output) {
  if (output == nullptr) {
    return;
  }

  output->clear_zenz_preceding_text_request_length();
  output->clear_zenz_following_text_request_length();

  if (request.preceding_length > 0) {
    output->set_zenz_preceding_text_request_length(request.preceding_length);
  }
  if (request.following_length > 0) {
    output->set_zenz_following_text_request_length(request.following_length);
  }
}

}  // namespace session
}  // namespace mozc
