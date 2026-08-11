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
// represent a client-observed structural boundary without falling back to the
// generic Mozc context.
struct ZenzClientContextView {
  absl::string_view preceding_text;
  absl::string_view following_text;
};

ZenzClientContextView GetZenzClientContextView(
    const commands::Context& context);

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CLIENT_CONTEXT_H_
