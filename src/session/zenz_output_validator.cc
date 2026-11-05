#include "session/zenz_output_validator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "base/util.h"

namespace mozc {
namespace session {
namespace {

ZenzValidationResult Accept(bool synthetic, std::string reason) {
  ZenzValidationResult result;
  result.accept = true;
  result.synthetic = synthetic;
  result.reason = std::move(reason);
  return result;
}

ZenzValidationResult Reject(std::string reason) {
  ZenzValidationResult result;
  result.accept = false;
  result.synthetic = false;
  result.reason = std::move(reason);
  return result;
}

bool DecodeOneUtf8(absl::string_view input, size_t* index, char32_t* cp) {
  if (*index >= input.size()) {
    return false;
  }

  const unsigned char c0 = static_cast<unsigned char>(input[*index]);

  if (c0 < 0x80) {
    *cp = c0;
    ++(*index);
    return true;
  }

  if ((c0 & 0xE0) == 0xC0) {
    if (*index + 1 >= input.size()) return false;
    const unsigned char c1 = static_cast<unsigned char>(input[*index + 1]);
    if ((c1 & 0xC0) != 0x80) return false;
    *cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    *index += 2;
    return true;
  }

  if ((c0 & 0xF0) == 0xE0) {
    if (*index + 2 >= input.size()) return false;
    const unsigned char c1 = static_cast<unsigned char>(input[*index + 1]);
    const unsigned char c2 = static_cast<unsigned char>(input[*index + 2]);
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
    *cp = ((c0 & 0x0F) << 12) |
          ((c1 & 0x3F) << 6) |
          (c2 & 0x3F);
    *index += 3;
    return true;
  }

  if ((c0 & 0xF8) == 0xF0) {
    if (*index + 3 >= input.size()) return false;
    const unsigned char c1 = static_cast<unsigned char>(input[*index + 1]);
    const unsigned char c2 = static_cast<unsigned char>(input[*index + 2]);
    const unsigned char c3 = static_cast<unsigned char>(input[*index + 3]);
    if ((c1 & 0xC0) != 0x80 ||
        (c2 & 0xC0) != 0x80 ||
        (c3 & 0xC0) != 0x80) {
      return false;
    }
    *cp = ((c0 & 0x07) << 18) |
          ((c1 & 0x3F) << 12) |
          ((c2 & 0x3F) << 6) |
          (c3 & 0x3F);
    *index += 4;
    return true;
  }

  return false;
}

struct Utf8CharForSymbolRestore {
  char32_t cp = 0;
  std::string bytes;
};

std::vector<Utf8CharForSymbolRestore> SplitUtf8ForSymbolRestore(
    absl::string_view text) {
  std::vector<Utf8CharForSymbolRestore> chars;
  size_t index = 0;
  while (index < text.size()) {
    const size_t begin = index;
    char32_t cp = 0;
    if (!DecodeOneUtf8(text, &index, &cp)) {
      return {};
    }
    chars.push_back({cp, std::string(text.substr(begin, index - begin))});
  }
  return chars;
}

int FindSymbolStyleVariantIndex(char32_t cp,
                                const std::vector<char32_t>& variants) {
  for (size_t i = 0; i < variants.size(); ++i) {
    if (variants[i] == cp) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool IsAsciiTokenCharForSymbolRestore(char32_t cp) {
  if (cp > 0x7F) {
    return false;
  }

  return (U'0' <= cp && cp <= U'9') ||
         (U'A' <= cp && cp <= U'Z') ||
         (U'a' <= cp && cp <= U'z') ||
         cp == U'_' || cp == U'-' || cp == U'.' || cp == U'+' ||
         cp == U'#' || cp == U'/' || cp == U'\\';
}

bool HasAsciiTokenNeighborForSymbolRestore(
    const std::vector<Utf8CharForSymbolRestore>& chars, size_t index) {
  if (index > 0 && IsAsciiTokenCharForSymbolRestore(chars[index - 1].cp)) {
    return true;
  }
  if (index + 1 < chars.size() &&
      IsAsciiTokenCharForSymbolRestore(chars[index + 1].cp)) {
    return true;
  }
  return false;
}

std::string FirstSymbolVariantBytes(
    const std::vector<Utf8CharForSymbolRestore>& chars, char32_t target) {
  for (const Utf8CharForSymbolRestore& ch : chars) {
    if (ch.cp == target) {
      return ch.bytes;
    }
  }
  return {};
}

bool ContainsAnySymbolStyleVariant(absl::string_view text,
                                   const std::vector<char32_t>& variants) {
  size_t index = 0;
  while (index < text.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8(text, &index, &cp)) {
      return false;
    }
    if (FindSymbolStyleVariantIndex(cp, variants) >= 0) {
      return true;
    }
  }
  return false;
}

std::string RestoreSymbolGroupStyle(
    absl::string_view style_source,
    absl::string_view zenz_value,
    const std::vector<char32_t>& variants,
    bool avoid_ascii_token_fallback_when_widening) {
  if (style_source.empty() || zenz_value.empty() || variants.empty()) {
    return std::string(zenz_value);
  }

  const std::vector<Utf8CharForSymbolRestore> source_chars =
      SplitUtf8ForSymbolRestore(style_source);
  const std::vector<Utf8CharForSymbolRestore> zenz_chars =
      SplitUtf8ForSymbolRestore(zenz_value);
  if (source_chars.empty() || zenz_chars.empty()) {
    return std::string(zenz_value);
  }

  std::vector<int> source_counts(variants.size(), 0);
  std::vector<int> zenz_counts(variants.size(), 0);

  for (const Utf8CharForSymbolRestore& ch : source_chars) {
    const int index = FindSymbolStyleVariantIndex(ch.cp, variants);
    if (index >= 0) {
      ++source_counts[index];
    }
  }
  for (const Utf8CharForSymbolRestore& ch : zenz_chars) {
    const int index = FindSymbolStyleVariantIndex(ch.cp, variants);
    if (index >= 0) {
      ++zenz_counts[index];
    }
  }

  int active_source_variant_count = 0;
  for (const int count : source_counts) {
    if (count > 0) {
      ++active_source_variant_count;
    }
  }
  if (active_source_variant_count == 0) {
    return std::string(zenz_value);
  }

  std::vector<int> missing_counts(variants.size(), 0);
  std::vector<int> excess_counts(variants.size(), 0);
  bool needs_restore = false;
  for (size_t i = 0; i < variants.size(); ++i) {
    missing_counts[i] = std::max(0, source_counts[i] - zenz_counts[i]);
    excess_counts[i] = std::max(0, zenz_counts[i] - source_counts[i]);
    needs_restore = needs_restore || missing_counts[i] > 0;
  }
  if (!needs_restore) {
    return std::string(zenz_value);
  }

  std::vector<std::string> default_replacements(variants.size());
  for (size_t i = 0; i < variants.size(); ++i) {
    if (missing_counts[i] > 0) {
      default_replacements[i] =
          FirstSymbolVariantBytes(source_chars, variants[i]);
      if (default_replacements[i].empty()) {
        missing_counts[i] = 0;
      }
    }
  }

  std::string restored;
  restored.reserve(zenz_value.size());
  for (size_t i = 0; i < zenz_chars.size(); ++i) {
    const Utf8CharForSymbolRestore& ch = zenz_chars[i];
    const int current_index =
        FindSymbolStyleVariantIndex(ch.cp, variants);

    int target_index = -1;
    bool positional_restore = false;
    if (current_index >= 0 && i < source_chars.size()) {
      const int source_index =
          FindSymbolStyleVariantIndex(source_chars[i].cp, variants);
      if (source_index >= 0 && source_index != current_index &&
          missing_counts[source_index] > 0) {
        target_index = source_index;
        positional_restore = true;
      }
    }

    if (target_index < 0 && current_index >= 0 &&
        excess_counts[current_index] > 0 && active_source_variant_count == 1) {
      for (size_t j = 0; j < variants.size(); ++j) {
        if (missing_counts[j] <= 0) {
          continue;
        }

        const bool widening_from_ascii =
            ch.cp <= 0x7F && variants[j] > 0x7F;
        if (avoid_ascii_token_fallback_when_widening && widening_from_ascii &&
            HasAsciiTokenNeighborForSymbolRestore(zenz_chars, i)) {
          continue;
        }

        target_index = static_cast<int>(j);
        break;
      }
    }

    if (target_index >= 0) {
      if (positional_restore) {
        restored.append(source_chars[i].bytes);
      } else {
        restored.append(default_replacements[target_index]);
      }
      --missing_counts[target_index];
      if (current_index >= 0 && excess_counts[current_index] > 0) {
        --excess_counts[current_index];
      }
      continue;
    }

    restored.append(ch.bytes);
  }

  return restored;
}

std::string RestoreSymbolGroupStyleFromKeyOrMozc(
    absl::string_view key,
    absl::string_view mozc_value,
    absl::string_view zenz_value,
    const std::vector<char32_t>& variants,
    bool avoid_ascii_token_fallback_when_widening) {
  absl::string_view style_source;
  if (ContainsAnySymbolStyleVariant(key, variants)) {
    style_source = key;
  } else if (ContainsAnySymbolStyleVariant(mozc_value, variants)) {
    style_source = mozc_value;
  } else {
    return std::string(zenz_value);
  }

  return RestoreSymbolGroupStyle(style_source, zenz_value, variants,
                                 avoid_ascii_token_fallback_when_widening);
}

const std::vector<std::vector<char32_t>>& UserControlledTrailingSymbolGroups() {
  static const std::vector<std::vector<char32_t>> kGroups = {
      {U'!', U'！'},
      {U'?', U'？'},
      {U'.', U'．', U'。'},
      {U'~', U'～', U'〜'},
      {U'…', U'‥'},
  };
  return kGroups;
}

int FindUserControlledTrailingSymbolGroup(
    char32_t cp,
    const std::vector<std::vector<char32_t>>& groups) {
  for (size_t i = 0; i < groups.size(); ++i) {
    if (FindSymbolStyleVariantIndex(cp, groups[i]) >= 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool CountUserControlledTrailingSymbols(
    absl::string_view text,
    const std::vector<std::vector<char32_t>>& groups,
    std::vector<int>* counts) {
  counts->assign(groups.size(), 0);
  if (text.empty()) {
    return true;
  }

  const std::vector<Utf8CharForSymbolRestore> chars =
      SplitUtf8ForSymbolRestore(text);
  if (chars.empty()) {
    return false;
  }

  // Only the trailing punctuation run authorizes trailing punctuation in Zenz
  // output.  Interior punctuation is lexical/technical context, not a budget
  // that Zenz may spend at the end.  For example, the period in "3.14" must
  // not authorize an added final period in "3.14.".
  for (size_t i = chars.size(); i > 0; --i) {
    const int group =
        FindUserControlledTrailingSymbolGroup(chars[i - 1].cp, groups);
    if (group < 0) {
      break;
    }
    ++(*counts)[group];
  }

  return true;
}

std::string SuppressUnrequestedTrailingSymbols(
    absl::string_view key,
    absl::string_view mozc_value,
    absl::string_view zenz_value) {
  if (zenz_value.empty()) {
    return std::string(zenz_value);
  }

  const std::vector<std::vector<char32_t>>& groups =
      UserControlledTrailingSymbolGroups();

  std::vector<int> key_counts;
  std::vector<int> mozc_counts;
  std::vector<int> zenz_counts;
  if (!CountUserControlledTrailingSymbols(key, groups, &key_counts) ||
      !CountUserControlledTrailingSymbols(mozc_value, groups, &mozc_counts) ||
      !CountUserControlledTrailingSymbols(zenz_value, groups, &zenz_counts)) {
    return std::string(zenz_value);
  }

  std::vector<int> allowed_counts(groups.size(), 0);
  for (size_t i = 0; i < groups.size(); ++i) {
    allowed_counts[i] = std::max(key_counts[i], mozc_counts[i]);
  }

  const std::vector<Utf8CharForSymbolRestore> zenz_chars =
      SplitUtf8ForSymbolRestore(zenz_value);
  if (zenz_chars.empty()) {
    return std::string(zenz_value);
  }

  std::vector<bool> remove(zenz_chars.size(), false);
  bool changed = false;

  // Only repair a trailing punctuation run. Interior punctuation can be part of
  // lexical or technical output such as "3.14", and deleting it would silently
  // change meaning. Within the trailing run, preserve at most the count already
  // present in the visible key or original Mozc value.
  for (size_t i = zenz_chars.size(); i > 0; --i) {
    const int group =
        FindUserControlledTrailingSymbolGroup(zenz_chars[i - 1].cp, groups);
    if (group < 0) {
      break;
    }

    if (zenz_counts[group] > allowed_counts[group]) {
      remove[i - 1] = true;
      --zenz_counts[group];
      changed = true;
    }
  }

  if (!changed) {
    return std::string(zenz_value);
  }

  std::string repaired;
  repaired.reserve(zenz_value.size());
  for (size_t i = 0; i < zenz_chars.size(); ++i) {
    if (!remove[i]) {
      repaired.append(zenz_chars[i].bytes);
    }
  }
  return repaired;
}

size_t LongestLeftContextSuffixPrefixChars(
    absl::string_view left_context, absl::string_view zenz_value) {
  if (left_context.empty() || zenz_value.empty()) {
    return 0;
  }

  const std::vector<Utf8CharForSymbolRestore> context_chars =
      SplitUtf8ForSymbolRestore(left_context);
  const std::vector<Utf8CharForSymbolRestore> value_chars =
      SplitUtf8ForSymbolRestore(zenz_value);
  if (context_chars.empty() || value_chars.empty()) {
    return 0;
  }

  const size_t max_overlap = std::min(context_chars.size(), value_chars.size());
  for (size_t overlap = max_overlap; overlap > 0; --overlap) {
    const size_t context_begin = context_chars.size() - overlap;
    bool matches = true;
    for (size_t i = 0; i < overlap; ++i) {
      if (context_chars[context_begin + i].bytes != value_chars[i].bytes) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return overlap;
    }
  }

  return 0;
}

bool LooksLikeLeftContextEcho(const ZenzValidationInput& input,
                              size_t key_len,
                              size_t value_len) {
  // A short overlap such as a particle or a common two-character word is not
  // strong enough evidence of context copying.  Four Japanese characters are
  // long enough to make accidental suffix/prefix overlap much less likely,
  // while still catching observed phrases such as "同じ試験を".
  constexpr size_t kMinContextEchoChars = 4;

  const size_t echo_len = LongestLeftContextSuffixPrefixChars(
      input.left_context, input.zenz_value);
  if (echo_len < kMinContextEchoChars) {
    return false;
  }

  const size_t baseline_len = std::max<size_t>(
      1, std::max(key_len, Util::CharsLen(input.mozc_value)));

  // A context prefix that is already much larger than the current composition
  // is itself strong echo evidence, even if the model returns only that copied
  // suffix.  Otherwise require the returned value to contain the context-sized
  // prefix plus at least one current-composition-sized payload.  These two
  // conditions target continuation/echo output while preserving legitimate
  // repeated words and short abbreviations whose output happens to equal a
  // context suffix.
  if (echo_len >= baseline_len * 2 + 1) {
    return true;
  }
  return value_len >= echo_len + baseline_len;
}

}  // namespace

std::string ZenzOutputValidator::RepairUserControlledSymbols(
    absl::string_view key,
    absl::string_view mozc_value,
    absl::string_view zenz_value) {
  const std::string restored =
      RestoreUserVisibleSymbolStyle(key, mozc_value, zenz_value);
  return SuppressUnrequestedTrailingSymbols(key, mozc_value, restored);
}

std::string ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
    absl::string_view key,
    absl::string_view mozc_value,
    absl::string_view zenz_value) {
  std::string restored(zenz_value);

  // Preserve the user's visible orthographic choice.  This is not fullwidth
  // normalization: if the source text used ASCII punctuation, a fullwidth Zenz
  // response is repaired back to ASCII; if the source text used Japanese-width
  // punctuation, an ASCII-normalized Zenz response is repaired back to that
  // source style.
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'~', U'～', U'〜'},
      /*avoid_ascii_token_fallback_when_widening=*/false);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'(', U'（'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U')', U'）'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'[', U'［'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U']', U'］'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'{', U'｛'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'}', U'｝'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'!', U'！'},
      /*avoid_ascii_token_fallback_when_widening=*/false);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'?', U'？'},
      /*avoid_ascii_token_fallback_when_widening=*/false);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U':', U'：'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U';', U'；'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U',', U'，'},
      /*avoid_ascii_token_fallback_when_widening=*/true);
  restored = RestoreSymbolGroupStyleFromKeyOrMozc(
      key, mozc_value, restored, {U'.', U'．'},
      /*avoid_ascii_token_fallback_when_widening=*/true);

  return restored;
}

bool ZenzOutputValidator::ContainsSpecialToken(absl::string_view text) {
  if (absl::StrContains(text, "<s>") ||
      absl::StrContains(text, "</s>") ||
      absl::StrContains(text, "<unk>") ||
      absl::StrContains(text, "<|endoftext|>")) {
    return true;
  }

  size_t index = 0;
  while (index < text.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8(text, &index, &cp)) {
      return true;
    }

    // zenz prompt private-use markers: U+EE00..U+EE06.
    if (0xEE00 <= cp && cp <= 0xEE06) {
      return true;
    }
  }

  return false;
}

bool ZenzOutputValidator::LooksLikeUrlOrEmail(absl::string_view text) {
  return absl::StrContains(text, "://") ||
         absl::StrContains(text, "www.") ||
         absl::StrContains(text, "@");
}

bool ZenzOutputValidator::LooksLikeSecret(absl::string_view text) {
  const size_t len = text.size();

  // Long ASCII-ish strings are often IDs, tokens, hashes, or keys.
  if (len >= 32) {
    size_t ascii_alnum = 0;
    for (unsigned char c : text) {
      if (('a' <= c && c <= 'z') ||
          ('A' <= c && c <= 'Z') ||
          ('0' <= c && c <= '9') ||
          c == '_' || c == '-' || c == '.') {
        ++ascii_alnum;
      }
    }
    if (ascii_alnum * 100 / len >= 80) {
      return true;
    }
  }

  return absl::StrContains(text, "password") ||
         absl::StrContains(text, "passwd") ||
         absl::StrContains(text, "secret") ||
         absl::StrContains(text, "token") ||
         absl::StrContains(text, "api_key") ||
         absl::StrContains(text, "apikey");
}

bool ZenzOutputValidator::IsObviousSurfaceTruncation(
    absl::string_view mozc_value, absl::string_view candidate_value) {
  return !mozc_value.empty() && !candidate_value.empty() &&
         candidate_value.size() < mozc_value.size() &&
         (absl::StartsWith(mozc_value, candidate_value) ||
          absl::EndsWith(mozc_value, candidate_value));
}

ZenzContextPrefixDecision ZenzOutputValidator::ResolveLeftContextPrefix(
    absl::string_view raw_value, absl::string_view left_context,
    absl::string_view mozc_value) {
  ZenzContextPrefixDecision decision;
  decision.value = std::string(raw_value);
  if (left_context.empty() || !absl::StartsWith(raw_value, left_context) ||
      raw_value == mozc_value) {
    return decision;
  }

  const absl::string_view stripped = raw_value.substr(left_context.size());
  if (!stripped.empty() && stripped == mozc_value) {
    decision.stripped = true;
    decision.value = std::string(stripped);
    return decision;
  }

  // The same surface prefix may be the user's own conversion or a context
  // echo.  Without reading evidence, neither removing nor retaining it is
  // safe: return the Mozc fallback instead of presenting a partial phrase.
  decision.safe = false;
  return decision;
}

namespace {

// Ranges with the Unicode Emoji property. Unlike a blanket
// supplementary-plane test, these do not classify CJK extensions as emoji.
// Text-default symbols are handled below only when emoji presentation is
// explicitly requested (VS16), apart from common expressive hearts/faces.
constexpr std::pair<char32_t, char32_t> kEmojiPropertyRanges[] = {
    {0x23, 0x23},
    {0x2a, 0x2a},
    {0x30, 0x39},
    {0xa9, 0xa9},
    {0xae, 0xae},
    {0x203c, 0x203c},
    {0x2049, 0x2049},
    {0x2122, 0x2122},
    {0x2139, 0x2139},
    {0x2194, 0x2199},
    {0x21a9, 0x21aa},
    {0x231a, 0x231b},
    {0x2328, 0x2328},
    {0x23cf, 0x23cf},
    {0x23e9, 0x23f3},
    {0x23f8, 0x23fa},
    {0x24c2, 0x24c2},
    {0x25aa, 0x25ab},
    {0x25b6, 0x25b6},
    {0x25c0, 0x25c0},
    {0x25fb, 0x25fe},
    {0x2600, 0x2604},
    {0x260e, 0x260e},
    {0x2611, 0x2611},
    {0x2614, 0x2615},
    {0x2618, 0x2618},
    {0x261d, 0x261d},
    {0x2620, 0x2620},
    {0x2622, 0x2623},
    {0x2626, 0x2626},
    {0x262a, 0x262a},
    {0x262e, 0x262f},
    {0x2638, 0x263a},
    {0x2640, 0x2640},
    {0x2642, 0x2642},
    {0x2648, 0x2653},
    {0x265f, 0x2660},
    {0x2663, 0x2663},
    {0x2665, 0x2666},
    {0x2668, 0x2668},
    {0x267b, 0x267b},
    {0x267e, 0x267f},
    {0x2692, 0x2697},
    {0x2699, 0x2699},
    {0x269b, 0x269c},
    {0x26a0, 0x26a1},
    {0x26a7, 0x26a7},
    {0x26aa, 0x26ab},
    {0x26b0, 0x26b1},
    {0x26bd, 0x26be},
    {0x26c4, 0x26c5},
    {0x26c8, 0x26c8},
    {0x26ce, 0x26cf},
    {0x26d1, 0x26d1},
    {0x26d3, 0x26d4},
    {0x26e9, 0x26ea},
    {0x26f0, 0x26f5},
    {0x26f7, 0x26fa},
    {0x26fd, 0x26fd},
    {0x2702, 0x2702},
    {0x2705, 0x2705},
    {0x2708, 0x270d},
    {0x270f, 0x270f},
    {0x2712, 0x2712},
    {0x2714, 0x2714},
    {0x2716, 0x2716},
    {0x271d, 0x271d},
    {0x2721, 0x2721},
    {0x2728, 0x2728},
    {0x2733, 0x2734},
    {0x2744, 0x2744},
    {0x2747, 0x2747},
    {0x274c, 0x274c},
    {0x274e, 0x274e},
    {0x2753, 0x2755},
    {0x2757, 0x2757},
    {0x2763, 0x2764},
    {0x2795, 0x2797},
    {0x27a1, 0x27a1},
    {0x27b0, 0x27b0},
    {0x27bf, 0x27bf},
    {0x2934, 0x2935},
    {0x2b05, 0x2b07},
    {0x2b1b, 0x2b1c},
    {0x2b50, 0x2b50},
    {0x2b55, 0x2b55},
    {0x3030, 0x3030},
    {0x303d, 0x303d},
    {0x3297, 0x3297},
    {0x3299, 0x3299},
    {0x1f004, 0x1f004},
    {0x1f0cf, 0x1f0cf},
    {0x1f170, 0x1f171},
    {0x1f17e, 0x1f17f},
    {0x1f18e, 0x1f18e},
    {0x1f191, 0x1f19a},
    {0x1f1e6, 0x1f1ff},
    {0x1f201, 0x1f202},
    {0x1f21a, 0x1f21a},
    {0x1f22f, 0x1f22f},
    {0x1f232, 0x1f23a},
    {0x1f250, 0x1f251},
    {0x1f300, 0x1f321},
    {0x1f324, 0x1f393},
    {0x1f396, 0x1f397},
    {0x1f399, 0x1f39b},
    {0x1f39e, 0x1f3f0},
    {0x1f3f3, 0x1f3f5},
    {0x1f3f7, 0x1f4fd},
    {0x1f4ff, 0x1f53d},
    {0x1f549, 0x1f54e},
    {0x1f550, 0x1f567},
    {0x1f56f, 0x1f570},
    {0x1f573, 0x1f57a},
    {0x1f587, 0x1f587},
    {0x1f58a, 0x1f58d},
    {0x1f590, 0x1f590},
    {0x1f595, 0x1f596},
    {0x1f5a4, 0x1f5a5},
    {0x1f5a8, 0x1f5a8},
    {0x1f5b1, 0x1f5b2},
    {0x1f5bc, 0x1f5bc},
    {0x1f5c2, 0x1f5c4},
    {0x1f5d1, 0x1f5d3},
    {0x1f5dc, 0x1f5de},
    {0x1f5e1, 0x1f5e1},
    {0x1f5e3, 0x1f5e3},
    {0x1f5e8, 0x1f5e8},
    {0x1f5ef, 0x1f5ef},
    {0x1f5f3, 0x1f5f3},
    {0x1f5fa, 0x1f64f},
    {0x1f680, 0x1f6c5},
    {0x1f6cb, 0x1f6d2},
    {0x1f6d5, 0x1f6d8},
    {0x1f6dc, 0x1f6e5},
    {0x1f6e9, 0x1f6e9},
    {0x1f6eb, 0x1f6ec},
    {0x1f6f0, 0x1f6f0},
    {0x1f6f3, 0x1f6fc},
    {0x1f7e0, 0x1f7eb},
    {0x1f7f0, 0x1f7f0},
    {0x1f90c, 0x1f93a},
    {0x1f93c, 0x1f945},
    {0x1f947, 0x1f9ff},
    {0x1fa70, 0x1fa7c},
    {0x1fa80, 0x1fa8a},
    {0x1fa8e, 0x1fac6},
    {0x1fac8, 0x1fac8},
    {0x1facd, 0x1fadc},
    {0x1fadf, 0x1faea},
    {0x1faef, 0x1faf8},
};

bool HasEmojiProperty(char32_t cp) {
  const auto* found = std::lower_bound(
      std::begin(kEmojiPropertyRanges),
      std::end(kEmojiPropertyRanges), cp,
      [](const auto& range, char32_t value) {
        return range.second < value;
      });
  return found != std::end(kEmojiPropertyRanges) &&
         found->first <= cp;
}

bool IsKeycapBase(char32_t cp) {
  return (cp >= U'0' && cp <= U'9') || cp == U'#' || cp == U'*';
}

bool IsEmojiCluster(absl::string_view cluster) {
  const std::u32string scalars = Util::Utf8ToUtf32(cluster);
  // Mozc's grapheme splitter is not a complete implementation of UAX #29.
  // It can merge a non-emoji prefix, ZWJ, and an emoji into one cluster.
  // Examine all scalars, not just the first one. Presentation selectors
  // belong to the immediately preceding scalar, not to the whole cluster.
  for (size_t i = 0; i < scalars.size(); ++i) {
    const char32_t cp = scalars[i];
    if (!HasEmojiProperty(cp)) {
      continue;
    }
    if (IsKeycapBase(cp)) {
      size_t next = i + 1;
      if (next < scalars.size() && scalars[next] == 0xFE0F) {
        ++next;
      }
      if (next < scalars.size() && scalars[next] == 0x20E3) {
        return true;
      }
      continue;
    }
    // These symbols commonly appear as technical text. Only count their
    // explicitly emoji-styled form, for compatibility with the old policy.
    if (cp == 0x00A9 || cp == 0x00AE || cp == 0x2122) {
      if (i + 1 < scalars.size() && scalars[i + 1] == 0xFE0F) {
        return true;
      }
      continue;
    }
    // Text presentation affects this scalar only; a following emoji in the
    // same imperfectly split cluster must still be detected.
    if (i + 1 < scalars.size() && scalars[i + 1] == 0xFE0E) {
      continue;
    }
    return true;
  }
  return false;
}

std::vector<std::string> ExtractEmojiClusters(absl::string_view text) {
  std::vector<std::string> graphemes;
  Util::SplitStringToUtf8Graphemes(text, &graphemes);
  std::vector<std::string> emoji;
  for (size_t i = 0; i < graphemes.size(); ++i) {
    const std::u32string scalars = Util::Utf8ToUtf32(graphemes[i]);
    // Mozc's grapheme splitter recognizes a keycap with VS16, but a keycap
    // without VS16 (e.g. 1 + U+20E3) can be split into two graphemes.
    if (!scalars.empty() && IsKeycapBase(scalars.back()) &&
        i + 1 < graphemes.size() &&
        graphemes[i + 1] == "\xE2\x83\xA3") {
      emoji.push_back(graphemes[i] + graphemes[i + 1]);
      ++i;
      continue;
    }
    if (IsEmojiCluster(graphemes[i])) {
      emoji.push_back(graphemes[i]);
    }
  }
  return emoji;
}

}  // namespace

bool ZenzOutputValidator::PreservesEmojiSequences(
    absl::string_view mozc_value, absl::string_view zenz_value) {
  if (!Util::IsValidUtf8(mozc_value) || !Util::IsValidUtf8(zenz_value)) {
    return false;
  }
  // Preserve the exact ordered list of user-visible emoji graphemes, not
  // merely their count. This blocks unsolicited insertions, substitutions,
  // deletions, duplicated emoji, and changes to skin-tone/ZWJ sequences.
  // Ordinary Japanese corrections around unchanged emoji remain possible.
  return ExtractEmojiClusters(mozc_value) == ExtractEmojiClusters(zenz_value);
}

ZenzValidationResult ZenzOutputValidator::ValidateReadingPreservation(
    absl::string_view key, absl::string_view normalized_key_reading,
    absl::string_view zenz_reading) {
  if (key.empty() || zenz_reading.empty()) {
    return Reject("reading_unavailable");
  }
  if (zenz_reading == key ||
      (!normalized_key_reading.empty() &&
       zenz_reading == normalized_key_reading)) {
    return Accept(false, "reading_preserved");
  }
  if (normalized_key_reading.empty()) {
    return Reject("reading_unavailable");
  }
  return Reject("reading_mismatch");
}

ZenzValidationResult ZenzOutputValidator::Validate(
    const ZenzValidationInput& input) const {
  if (input.key.empty()) {
    return Reject("empty_key");
  }

  if (input.zenz_value.empty()) {
    return Reject("empty_value");
  }

  if (input.zenz_value == input.mozc_value) {
    return Reject("same_as_mozc");
  }

  if (Util::CharsLen(input.key) < input.min_key_length) {
    return Reject("too_short_key");
  }

  if (ContainsSpecialToken(input.zenz_value)) {
    return Reject("special_token");
  }

  if (!Util::IsValidUtf8(input.zenz_value)) {
    return Reject("invalid_utf8");
  }

  if (!input.allow_zenz_emoji_changes &&
      !PreservesEmojiSequences(input.mozc_value, input.zenz_value)) {
    return Reject("emoji_sequence_changed");
  }

  const size_t key_len = Util::CharsLen(input.key);
  const size_t value_len = Util::CharsLen(input.zenz_value);

  if (value_len == 0) {
    return Reject("zero_value_len");
  }

  if (LooksLikeLeftContextEcho(input, key_len, value_len)) {
    return Reject("left_context_echo");
  }

  // Conservative length guard. Japanese conversion can shrink/expand, but not
  // arbitrarily.
  if (value_len > key_len * 3 + 8) {
    return Reject("too_long_value");
  }

  if (LooksLikeUrlOrEmail(input.zenz_value)) {
    return Reject("url_or_email");
  }

  if (LooksLikeSecret(input.zenz_value)) {
    return Reject("secret_like");
  }

  if (!input.allow_synthetic_candidate) {
    return Reject("synthetic_disabled");
  }

  return Accept(/*synthetic=*/true, "accepted_synthetic");
}

}  // namespace session
}  // namespace mozc
