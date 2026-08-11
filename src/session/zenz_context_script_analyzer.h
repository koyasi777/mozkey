#ifndef MOZC_SESSION_ZENZ_CONTEXT_SCRIPT_ANALYZER_H_
#define MOZC_SESSION_ZENZ_CONTEXT_SCRIPT_ANALYZER_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

// Unicode-codepoint-level script profile for Zenz surrounding context.
//
// The profile intentionally contains more information than the persistent
// context_class strings. Richer analysis is mapped onto the existing class
// vocabulary so stored feedback remains compatible.
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
  // This deliberately preserves the historical acceptance envelope as
  // closely as possible. The old implementation counted a typical Japanese
  // BMP character as three non-ASCII UTF-8 bytes, so a genuine Japanese
  // script codepoint is weighted by three here.
  bool LooksMostlyJapanese(
      const ZenzContextScriptProfile& profile) const;

  // Returns true for natural Japanese context containing embedded ASCII
  // technical terms, product names, versions, and ordinary numbers.
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
