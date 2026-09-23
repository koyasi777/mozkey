#include "session/zenz_output_validator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

ZenzValidationResult ZenzOutputValidator::ValidateReadingPreservation(
    absl::string_view key,
    absl::string_view normalized_key_reading,
    absl::string_view mozc_reading,
    absl::string_view zenz_reading) {
  if (key.empty() || zenz_reading.empty()) {
    return Reject("reading_unavailable");
  }
  if (zenz_reading == key) {
    return Accept(false, "reading_preserved");
  }

  bool has_reference_reading = false;
  if (!normalized_key_reading.empty()) {
    has_reference_reading = true;
    if (zenz_reading == normalized_key_reading) {
      return Accept(false, "reading_preserved");
    }
  }
  if (!mozc_reading.empty()) {
    has_reference_reading = true;
    if (zenz_reading == mozc_reading) {
      return Accept(false, "reading_preserved");
    }
  }

  if (!has_reference_reading) {
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
