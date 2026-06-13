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

  // Restores user-visible Japanese symbol style that Zenz may normalize to
  // ASCII.  This currently protects wave-dash-like Japanese tildes: when the
  // key or the original Mozc value contains U+FF5E FULLWIDTH TILDE or U+301C
  // WAVE DASH, and Zenz returns U+007E ASCII TILDE instead, the returned value
  // is restored before validation, display, commit, and feedback recording.
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
