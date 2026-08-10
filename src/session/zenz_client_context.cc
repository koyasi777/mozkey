#include "session/zenz_client_context.h"

namespace mozc {
namespace session {

ZenzClientContextView GetZenzClientContextView(
    const commands::Context& context) {
  ZenzClientContextView result;

  result.preceding_text =
      context.has_zenz_preceding_text()
          ? absl::string_view(context.zenz_preceding_text())
          : absl::string_view(context.preceding_text());

  result.following_text =
      context.has_zenz_following_text()
          ? absl::string_view(context.zenz_following_text())
          : absl::string_view(context.following_text());

  return result;
}

}  // namespace session
}  // namespace mozc