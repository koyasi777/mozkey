#include "session/zenz_context_sanitizer.h"

#include <cstddef>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "base/util.h"
#include "session/zenz_context_script_analyzer.h"

namespace mozc {
namespace session {
namespace {

bool IsAsciiDigit(unsigned char c) {
  return absl::ascii_isdigit(c);
}

bool IsAsciiVisible(unsigned char c) {
  return 0x21 <= c && c <= 0x7e;
}

std::string TruncateRightByChars(
    absl::string_view text,
    size_t max_chars) {
  if (max_chars == 0 || text.empty()) {
    return "";
  }

  const size_t len = Util::CharsLen(text);
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

bool ZenzContextSanitizer::ContainsLongAsciiRun(
    absl::string_view text) {
  size_t run = 0;

  for (const unsigned char c : text) {
    if (IsAsciiVisible(c)) {
      ++run;

      if (run >= 8) {
        return true;
      }
    } else {
      run = 0;
    }
  }

  return false;
}

bool ZenzContextSanitizer::ContainsLongDigitRun(
    absl::string_view text) {
  size_t run = 0;

  for (const unsigned char c : text) {
    if (IsAsciiDigit(c)) {
      ++run;

      if (run >= 4) {
        return true;
      }
    } else {
      run = 0;
    }
  }

  return false;
}

bool ZenzContextSanitizer::ContainsSensitiveAsciiPattern(
    absl::string_view text) {
  const std::string lower =
      absl::AsciiStrToLower(std::string(text));

  if (absl::StrContains(lower, "password") ||
      absl::StrContains(lower, "passwd") ||
      absl::StrContains(lower, "pwd") ||
      absl::StrContains(lower, "token") ||
      absl::StrContains(lower, "secret") ||
      absl::StrContains(lower, "apikey") ||
      absl::StrContains(lower, "api_key") ||
      absl::StrContains(lower, "authorization") ||
      absl::StrContains(lower, "bearer ") ||
      absl::StrContains(lower, "cookie") ||
      absl::StrContains(lower, "sessionid") ||
      absl::StrContains(lower, "session_id")) {
    return true;
  }

  if (absl::StrContains(lower, "http://") ||
      absl::StrContains(lower, "https://") ||
      absl::StrContains(lower, "www.") ||
      absl::StrContains(lower, "mailto:")) {
    return true;
  }

  if (absl::StrContains(lower, "@") &&
      absl::StrContains(lower, ".")) {
    return true;
  }

  if (absl::StrContains(lower, "c:\\") ||
      absl::StrContains(lower, "\\users\\") ||
      absl::StrContains(lower, "/home/") ||
      absl::StrContains(lower, "/users/")) {
    return true;
  }

  if (absl::StrContains(lower, "sk-") ||
      absl::StrContains(lower, "pk_") ||
      absl::StrContains(lower, "ghp_") ||
      absl::StrContains(lower, "xoxb-")) {
    return true;
  }

  return ContainsLongAsciiRun(text) ||
         ContainsLongDigitRun(text);
}

ZenzContextSanitizationResult
ZenzContextSanitizer::SanitizeForZenz(
    absl::string_view raw_context,
    size_t max_chars) const {
  ZenzContextSanitizationResult result;

  if (raw_context.empty() || max_chars == 0) {
    result.context_class = "empty";
    result.reason = "empty_context";
    return result;
  }

  const std::string truncated =
      TruncateRightByChars(
          raw_context,
          max_chars);

  // Privacy remains an independent gate. Phase C1 intentionally does not
  // modify any existing sensitive-pattern rule or threshold.
  if (ContainsSensitiveAsciiPattern(truncated)) {
    result.context_class = "sensitive_like";
    result.reason = "sensitive_context_rejected";
    return result;
  }

  const ZenzContextScriptProfile script_profile =
      script_analyzer_.Analyze(truncated);

  result.context_class =
      script_analyzer_.ClassifyForContextClass(
          script_profile);

  // Never allow raw context to be persisted. Learning may use context_class
  // only, which is non-reversible.
  result.allowed_for_learning = false;

  if (!script_analyzer_.LooksMostlyJapanese(
          script_profile)) {
    result.reason =
        "non_japanese_context_rejected";
    return result;
  }

  result.sanitized_context = truncated;
  result.allowed_for_prompt = true;
  result.reason = "context_allowed";

  return result;
}

}  // namespace session
}  // namespace mozc