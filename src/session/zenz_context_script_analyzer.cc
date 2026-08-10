#include "session/zenz_context_script_analyzer.h"

#include <cstddef>
#include <limits>
#include <string>

#include "absl/strings/string_view.h"
#include "base/util.h"

namespace mozc {
namespace session {
namespace {

bool IsJapaneseScriptType(const Util::ScriptType type) {
  switch (type) {
    case Util::HIRAGANA:
    case Util::KATAKANA:
    case Util::KANJI:
      return true;

    default:
      return false;
  }
}

bool IsAsciiAlphaNum(const char32_t codepoint) {
  return (U'0' <= codepoint && codepoint <= U'9') ||
         (U'A' <= codepoint && codepoint <= U'Z') ||
         (U'a' <= codepoint && codepoint <= U'z');
}

bool IsAsciiVisible(const char32_t codepoint) {
  return 0x21 <= codepoint && codepoint <= 0x7e;
}

size_t SaturatingMultiply(
    const size_t value,
    const size_t multiplier) {
  constexpr size_t kMax =
      std::numeric_limits<size_t>::max();

  if (value == 0 || multiplier == 0) {
    return 0;
  }

  if (value > kMax / multiplier) {
    return kMax;
  }

  return value * multiplier;
}

size_t SaturatingAdd(
    const size_t lhs,
    const size_t rhs) {
  constexpr size_t kMax =
      std::numeric_limits<size_t>::max();

  if (lhs > kMax - rhs) {
    return kMax;
  }

  return lhs + rhs;
}

}  // namespace

ZenzContextScriptProfile ZenzContextScriptAnalyzer::Analyze(
    const absl::string_view text) const {
  ZenzContextScriptProfile profile;

  for (ConstChar32Iterator iter(text); !iter.Done(); iter.Next()) {
    const char32_t codepoint = iter.Get();
    ++profile.total_chars;

    const Util::ScriptType type =
        Util::GetScriptType(codepoint);

    if (IsJapaneseScriptType(type)) {
      ++profile.japanese_script_chars;
    } else {
      switch (type) {
        case Util::NUMBER:
        case Util::ALPHABET:
          ++profile.non_japanese_alnum_chars;
          break;

        case Util::EMOJI:
          ++profile.emoji_chars;
          break;

        default:
          ++profile.other_chars;
          break;
      }
    }

    if (IsAsciiVisible(codepoint)) {
      ++profile.ascii_visible_chars;

      if (!IsAsciiAlphaNum(codepoint)) {
        ++profile.ascii_symbol_chars;
      }
    }
  }

  return profile;
}

bool ZenzContextScriptAnalyzer::LooksMostlyJapanese(
    const ZenzContextScriptProfile& profile) const {
  if (profile.japanese_script_chars == 0) {
    return false;
  }

  // Preserve the approximate strength of the previous UTF-8-byte-based
  // Japanese signal while making every competing signal explicit.
  //
  // Genuine Japanese scripts are strong positive evidence.
  // Latin/number characters are strong competing linguistic evidence.
  // Emoji and UNKNOWN_SCRIPT characters are weak competing evidence:
  // punctuation, whitespace and emoji remain usable in normal Japanese text,
  // but a large amount of non-Japanese or symbol content must not be allowed
  // to ride on a single Japanese character.
  const size_t japanese_signal =
      SaturatingMultiply(
          profile.japanese_script_chars,
          3);

  size_t competing_signal =
      SaturatingMultiply(
          profile.non_japanese_alnum_chars,
          2);

  competing_signal =
      SaturatingAdd(
          competing_signal,
          profile.emoji_chars);

  competing_signal =
      SaturatingAdd(
          competing_signal,
          profile.other_chars);

  return japanese_signal >= competing_signal &&
         profile.non_japanese_alnum_chars <= 4 &&
         profile.ascii_visible_chars <= japanese_signal;
}

std::string ZenzContextScriptAnalyzer::ClassifyForContextClass(
    const ZenzContextScriptProfile& profile) const {
  if (profile.total_chars == 0) {
    return "empty";
  }

  if (profile.japanese_script_chars > 0) {
    if (profile.non_japanese_alnum_chars > 0) {
      // Keep the legacy class name for persistent-feedback compatibility.
      // It now also covers full-width Latin letters/numbers.
      return "mixed_japanese_ascii";
    }

    if (profile.ascii_symbol_chars > 0) {
      return "japanese_with_punctuation";
    }

    // Non-ASCII punctuation, emoji, variation selectors, etc. intentionally
    // do not create a new persistent class in this phase.
    return "japanese_only";
  }

  if (profile.non_japanese_alnum_chars > 0) {
    // Keep the legacy class name even though full-width Latin/number is now
    // recognized correctly as non-Japanese alphanumeric text.
    return "ascii_or_digit";
  }

  return "symbol_or_other";
}

}  // namespace session
}  // namespace mozc