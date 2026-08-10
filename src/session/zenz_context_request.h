// Copyright 2026
// Licensed under the same terms as Mozc.

#ifndef MOZC_SESSION_ZENZ_CONTEXT_REQUEST_H_
#define MOZC_SESSION_ZENZ_CONTEXT_REQUEST_H_

#include <cstdint>

#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"

namespace mozc {
namespace session {

// Acquisition budgets requested from a platform client. These are deliberately
// capped independently from Session's semantic selector configuration so a
// malformed/manual config cannot cause an unbounded synchronous platform read.
struct ZenzContextRequest {
  uint32_t preceding_length = 0;
  uint32_t following_length = 0;
};

// Returns the bounded surrounding-text acquisition request for an upcoming
// client-context snapshot.
//
// `may_snapshot_client_context` should be true only while the current session
// can enter the precomposition path that snapshots client surrounding text.
// PASSWORD fields never request extended surrounding text.
ZenzContextRequest GetZenzContextRequest(
    const config::Config& config,
    commands::Context::InputFieldType input_field_type,
    bool may_snapshot_client_context);

// Attaches only nonzero request lengths. Existing request fields are cleared
// first, making this safe for reused Output objects.
void AttachZenzContextRequest(const ZenzContextRequest& request,
                              commands::Output* output);

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CONTEXT_REQUEST_H_