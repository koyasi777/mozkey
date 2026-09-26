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
};

struct ZenzValidationResult {
  bool accept = false;
  bool synthetic = false;
  std::string reason;
};

class ZenzOutputValidator {
 public:
  ZenzValidationResult Validate(const ZenzValidationInput& input) const;

  // Enforces the IME boundary between conversion and prediction.  The final
  // adopted Zenz surface must reverse to the current typed key, to the same
  // normalized reading obtained by reverse-converting that key, or to the same
  // reverse reading as the trusted Mozc baseline.  The baseline fallback keeps
  // user-dictionary / technical surfaces working when their visible spelling
  // does not reverse back to the literal composition key.
  static ZenzValidationResult ValidateReadingPreservation(
      absl::string_view key,
      absl::string_view normalized_key_reading,
      absl::string_view mozc_reading,
      absl::string_view zenz_reading);

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
