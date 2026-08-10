#include "session/zenz_context_script_analyzer.h"

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

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

bool IsAsciiTechnicalConnector(
    const char32_t codepoint) {
  switch (codepoint) {
    case U'-':
    case U'.':
    case U'/':
    case U'+':
    case U'#':
    case U'_':
      return true;

    default:
      return false;
  }
}

bool IsAsciiTechnicalTokenChar(
    const char32_t codepoint) {
  return IsAsciiAlphaNum(codepoint) ||
         IsAsciiTechnicalConnector(codepoint);
}

bool IsJapaneseNumericUnit(
    const char32_t codepoint) {
  switch (codepoint) {
    case U'年':
    case U'月':
    case U'日':
    case U'時':
    case U'分':
    case U'秒':
    case U'円':
    case U'歳':
    case U'人':
    case U'個':
    case U'回':
    case U'件':
    case U'台':
    case U'号':
    case U'階':
      return true;

    default:
      return false;
  }
}

bool IsJapaneseTechnicalNominalSuffix(
    const char32_t codepoint) {
  switch (codepoint) {
    case U'版':
    case U'型':
    case U'系':
    case U'用':
      return true;

    default:
      return false;
  }
}

struct MixedScriptStructure {
  size_t ascii_alnum_chars = 0;
  size_t attached_ascii_alnum_chars = 0;
  size_t technical_symbol_chars = 0;
  size_t max_japanese_run_chars = 0;

  bool has_hiragana_attachment = false;
  bool has_numeric_unit_attachment = false;
  bool has_nominal_suffix_attachment = false;
};

MixedScriptStructure AnalyzeMixedScriptStructure(
    const absl::string_view text) {
  std::vector<char32_t> codepoints;

  for (ConstChar32Iterator iter(text);
       !iter.Done();
       iter.Next()) {
    codepoints.push_back(iter.Get());
  }

  MixedScriptStructure result;

  size_t current_japanese_run = 0;

  for (const char32_t codepoint : codepoints) {
    if (IsJapaneseScriptType(
            Util::GetScriptType(codepoint))) {
      ++current_japanese_run;

      if (current_japanese_run >
          result.max_japanese_run_chars) {
        result.max_japanese_run_chars =
            current_japanese_run;
      }
    } else {
      current_japanese_run = 0;
    }
  }

  size_t i = 0;

  while (i < codepoints.size()) {
    if (!IsAsciiAlphaNum(codepoints[i])) {
      ++i;
      continue;
    }

    const size_t token_begin = i;

    size_t token_ascii_alnum_chars = 0;
    size_t token_symbol_chars = 0;
    bool token_is_all_digits = true;

    while (i < codepoints.size() &&
           IsAsciiTechnicalTokenChar(
               codepoints[i])) {
      const char32_t codepoint =
          codepoints[i];

      if (IsAsciiAlphaNum(codepoint)) {
        ++token_ascii_alnum_chars;

        if (!(U'0' <= codepoint &&
              codepoint <= U'9')) {
          token_is_all_digits = false;
        }
      } else {
        ++token_symbol_chars;
      }

      ++i;
    }

    result.ascii_alnum_chars +=
        token_ascii_alnum_chars;

    result.technical_symbol_chars +=
        token_symbol_chars;

    const bool left_is_japanese =
        token_begin > 0 &&
        IsJapaneseScriptType(
            Util::GetScriptType(
                codepoints[token_begin - 1]));

    const bool right_is_japanese =
        i < codepoints.size() &&
        IsJapaneseScriptType(
            Util::GetScriptType(
                codepoints[i]));

    if (left_is_japanese ||
        right_is_japanese) {
      result.attached_ascii_alnum_chars +=
          token_ascii_alnum_chars;
    }

    if (right_is_japanese) {
      const char32_t right =
          codepoints[i];

      const Util::ScriptType right_type =
          Util::GetScriptType(right);

      if (right_type == Util::HIRAGANA) {
        result.has_hiragana_attachment =
            true;
      }

      if (token_is_all_digits &&
          IsJapaneseNumericUnit(right)) {
        result.has_numeric_unit_attachment =
            true;
      }

      if (IsJapaneseTechnicalNominalSuffix(
              right)) {
        result.has_nominal_suffix_attachment =
            true;
      }
    }
  }

  return result;
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

bool ZenzContextScriptAnalyzer::LooksUsableAsJapaneseContext(
    const absl::string_view text,
    const ZenzContextScriptProfile& profile) const {
  // Preserve everything already accepted by the C1 policy.
  if (LooksMostlyJapanese(profile)) {
    return true;
  }

  if (profile.japanese_script_chars == 0 ||
      profile.non_japanese_alnum_chars == 0) {
    return false;
  }

  const MixedScriptStructure structure =
      AnalyzeMixedScriptStructure(text);

  // The relaxed path is intentionally limited to ASCII technical tokens.
  // Full-width Latin/number or other non-Japanese scripts do not receive the
  // technical-token exemption.
  if (structure.ascii_alnum_chars !=
      profile.non_japanese_alnum_chars) {
    return false;
  }

  // Every alphanumeric token that needs the relaxation must participate in a
  // direct Japanese boundary. This prevents an unrelated English span from
  // becoming acceptable merely because a short Japanese fragment occurs
  // elsewhere in the context.
  if (structure.attached_ascii_alnum_chars !=
      structure.ascii_alnum_chars) {
    return false;
  }

  // Technical punctuation inside tokens such as GPT-5, UTF-8, HTTP/2, C++ and
  // v3.2 is not unrelated-language evidence. Other punctuation, UNKNOWN_SCRIPT
  // content and emoji remain weak counterevidence.
  const size_t unrelated_other_chars =
      profile.other_chars >
              structure.technical_symbol_chars
          ? profile.other_chars -
                structure.technical_symbol_chars
          : 0;

  const size_t weak_counterevidence =
      SaturatingAdd(
          unrelated_other_chars,
          profile.emoji_chars);

  const size_t weak_counterevidence_limit =
      SaturatingMultiply(
          profile.japanese_script_chars,
          3);

  if (weak_counterevidence >
      weak_counterevidence_limit) {
    return false;
  }

  // Two or more contiguous Japanese-script characters provide enough local
  // linguistic structure to license an attached technical token.
  if (structure.max_japanese_run_chars >= 2) {
    return true;
  }

  // A hiragana boundary is strong grammatical evidence. It covers ordinary
  // particles and inflectional continuation such as Windows11を, M1で and
  // v3.2へ even when the available surrounding-context slice is short.
  if (structure.has_hiragana_attachment) {
    return true;
  }

  // A pure number followed by a Japanese counter/time unit is a common
  // Japanese construction even with only one Japanese-script codepoint:
  // 2026年, 8月, 10時, 30分, etc.
  if (structure.has_numeric_unit_attachment) {
    return true;
  }

  // Technical nominal suffixes are similarly productive in Japanese:
  // v3.2版, ARM64版, X系, etc.
  if (structure.has_nominal_suffix_attachment) {
    return true;
  }

  return false;
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