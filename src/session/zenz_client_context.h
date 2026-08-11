#ifndef MOZC_SESSION_ZENZ_CLIENT_CONTEXT_H_
#define MOZC_SESSION_ZENZ_CLIENT_CONTEXT_H_

#include "absl/strings/string_view.h"
#include "protocol/commands.pb.h"

namespace mozc {
namespace session {

// Non-owning raw context selected specifically for Zenz.
//
// Extended fields take precedence per side when present.  Presence, rather
// than non-emptiness, is used so an explicitly empty extended value can
// suppress generic fallback. This represents either a client-observed
// structural boundary or a deliberate decision not to expose platform text
// to Zenz.
struct ZenzClientContextView {
  absl::string_view preceding_text;
  absl::string_view following_text;
};

ZenzClientContextView GetZenzClientContextView(
    const commands::Context& context);

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CLIENT_CONTEXT_H_
