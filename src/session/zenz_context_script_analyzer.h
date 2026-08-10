#ifndef MOZC_SESSION_ZENZ_CONTEXT_SCRIPT_ANALYZER_H_
#define MOZC_SESSION_ZENZ_CONTEXT_SCRIPT_ANALYZER_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

// Unicode-codepoint-level script profile for Zenz surrounding context.
//
// The profile intentionally contains more information than the current
// persistent context_class strings. Persistent feedback compatibility is
// preserved in Phase C1; richer policy decisions can use this profile later.
struct ZenzContextScriptProfile {
  size_t total_chars = 0;
  size_t japanese_script_chars = 0;
  size_t non_japanese_alnum_chars = 0;
  size_t ascii_visible_chars = 0;
  size_t ascii_symbol_chars = 0;
  size_t emoji_chars = 0;
  size_t other_chars = 0;
};

class ZenzContextScriptAnalyzer {
 public:
  ZenzContextScriptProfile Analyze(absl::string_view text) const;

  // Returns whether the context contains enough genuine Japanese-script
  // signal to be used as Zenz linguistic context.
  //
  // Phase C1 deliberately preserves the previous acceptance envelope as
  // closely as possible. The old implementation counted a typical Japanese
  // BMP character as three non-ASCII UTF-8 bytes, so a genuine Japanese
  // script codepoint is weighted by three here. Policy-threshold tuning is
  // intentionally deferred to a separate phase.
  bool LooksMostlyJapanese(
      const ZenzContextScriptProfile& profile) const;

  // Candidate policy for natural Japanese context containing embedded ASCII
  // technical terms, product names, versions and ordinary numbers.
  //
  // C3A does not route production sanitization through this method. It is
  // defined and characterized independently before the production switch.
  bool LooksUsableAsJapaneseContext(
      absl::string_view text,
      const ZenzContextScriptProfile& profile) const;

  // Maps the richer Unicode profile onto the existing persistent context
  // class vocabulary. Do not change these strings here without a feedback
  // compatibility/migration design.
  std::string ClassifyForContextClass(
      const ZenzContextScriptProfile& profile) const;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CONTEXT_SCRIPT_ANALYZER_H_