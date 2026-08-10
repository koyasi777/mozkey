#include "session/zenz_context_sanitizer.h"

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "base/util.h"
#include "session/zenz_context_script_analyzer.h"
#include "session/zenz_text_privacy_analyzer.h"

namespace mozc {
namespace session {
namespace {

std::string TruncateRightByChars(
    absl::string_view text,
    size_t max_chars) {
  if (max_chars == 0 ||
      text.empty()) {
    return "";
  }

  const size_t len =
      Util::CharsLen(text);

  if (len <= max_chars) {
    return std::string(text);
  }

  return std::string(
      Util::Utf8SubString(
          text,
          len - max_chars,
          max_chars));
}

}  // namespace

ZenzContextSanitizationResult
ZenzContextSanitizer::SanitizeForZenz(
    const absl::string_view raw_context,
    const size_t max_chars) const {
  ZenzContextSanitizationResult result;

  if (raw_context.empty() ||
      max_chars == 0) {
    result.context_class = "empty";
    result.reason = "empty_context";
    return result;
  }

  const std::string truncated =
      TruncateRightByChars(
          raw_context,
          max_chars);

  // Surrounding context uses the structural privacy policy. Ordinary technical
  // text is not sensitive merely because it contains four digits or eight
  // visible ASCII characters, while credential, address, path, secret-prefix
  // and opaque-identifier structures remain protected.
  const ZenzTextPrivacyAnalysis privacy =
      privacy_analyzer_.Analyze(
          truncated,
          ZenzTextPrivacyPolicy::
              kContext);

  if (privacy.sensitive()) {
    result.context_class =
        "sensitive_like";
    result.reason =
        "sensitive_context_rejected";
    return result;
  }

  const ZenzContextScriptProfile
      script_profile =
          script_analyzer_.Analyze(
              truncated);

  result.context_class =
      script_analyzer_.
          ClassifyForContextClass(
              script_profile);

  // Raw context is never persisted.
  result.allowed_for_learning = false;

  if (!script_analyzer_.
          LooksUsableAsJapaneseContext(
              truncated,
              script_profile)) {
    result.reason =
        "non_japanese_context_rejected";
    return result;
  }

  result.sanitized_context =
      truncated;
  result.allowed_for_prompt = true;
  result.reason =
      "context_allowed";

  return result;
}

}  // namespace session
}  // namespace mozc