#ifndef MOZC_SESSION_ZENZ_OUTPUT_VALIDATOR_H_
#define MOZC_SESSION_ZENZ_OUTPUT_VALIDATOR_H_

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

struct ZenzValidationInput {
  std::string key;
  std::string mozc_value;
  std::string zenz_value;
  std::string left_context;

  uint32_t min_key_length = 5;
  bool allow_synthetic_candidate = true;
  bool allow_zenz_emoji_changes = false;
};

struct ZenzValidationResult {
  bool accept = false;
  bool synthetic = false;
  std::string reason;
};

// Resolution of a Zenz result that begins with the left-context surface.
// Ambiguity is a reason to fall back to the already computed Mozc conversion.
struct ZenzContextPrefixDecision {
  bool safe = true;
  bool stripped = false;
  std::string value;
};

class ZenzOutputValidator {
 public:
  ZenzValidationResult Validate(const ZenzValidationInput& input) const;

  // An unchanged ordered sequence of emoji graphemes is allowed, including
  // emoji already selected by the user in the Mozc baseline. This policy
  // rejects unsolicited emoji insertion, deletion, or substitution.
  static bool PreservesEmojiSequences(absl::string_view mozc_value,
                                      absl::string_view zenz_value);

  // Strict opt-in verifier: compare only with the complete typed key and its
  // normalized reading. A Mozc display candidate is NOT an admissible anchor,
  // since that candidate might already omit part of the typed reading.
  // A reverse-conversion failure fails closed only when strict mode is ON.
  static ZenzValidationResult ValidateReadingPreservation(
      absl::string_view key, absl::string_view normalized_key_reading,
      absl::string_view zenz_reading);

  // Rejects a strict prefix/suffix of Mozc's complete visible conversion.
  // This cheap guard detects obvious omissions, not arbitrary semantic loss.
  static bool IsObviousSurfaceTruncation(
      absl::string_view mozc_value, absl::string_view candidate_value);

  // Never delete the left context merely because it prefixes the output.
  // A prefix can be stripped only if doing so recovers the exact Mozc baseline.
  // Other overlapping outputs are ambiguous without a reading-coverage proof.
  static ZenzContextPrefixDecision ResolveLeftContextPrefix(
      absl::string_view raw_value, absl::string_view left_context,
      absl::string_view mozc_value);

  // Repairs Zenz output so that sentence-final or expressive punctuation stays
  // under user control. Existing punctuation style is restored first, then
  // excess punctuation in the trailing punctuation run is removed when the
  // trailing punctuation run of neither the visible input nor the original
  // Mozc value contained that many symbols. Interior punctuation never
  // authorizes new trailing punctuation.
  //
  // Interior punctuation is intentionally left untouched because it can be
  // lexical or technical (for example, "3.14"). This function is intended to
  // be the common ingress contract for model output and learned Zenz feedback.
  static std::string RepairUserControlledSymbols(
      absl::string_view key,
      absl::string_view mozc_value,
      absl::string_view zenz_value);

  // Restores user-visible symbol style that Zenz may normalize. This preserves
  // the user's current composition style instead of normalizing to either
  // fullwidth or ASCII: if the key or original Mozc value used Japanese-width
  // punctuation, ASCII-normalized Zenz punctuation is restored; if the source
  // used ASCII punctuation, fullwidth Zenz punctuation is restored to ASCII.
  // The returned value is used before validation, display, commit, and
  // feedback recording.
  static std::string RestoreUserVisibleSymbolStyle(
      absl::string_view key,
      absl::string_view mozc_value,
      absl::string_view zenz_value);

 private:
  static bool ContainsSpecialToken(absl::string_view text);
  static bool LooksLikeUrlOrEmail(absl::string_view text);
  static bool LooksLikeSecret(absl::string_view text);
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_OUTPUT_VALIDATOR_H_
