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

bool IsAsciiTilde(char32_t cp) { return cp == 0x007E; }

bool IsJapaneseWaveDash(char32_t cp) {
  return cp == 0xFF5E ||  // FULLWIDTH TILDE: ～
         cp == 0x301C;    // WAVE DASH: 〜
}

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

int CountCodePoint(absl::string_view text, char32_t target) {
  int count = 0;
  size_t index = 0;
  while (index < text.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8(text, &index, &cp)) {
      return 0;
    }
    if (cp == target) {
      ++count;
    }
  }
  return count;
}

int CountJapaneseWaveDashFamily(absl::string_view text) {
  return CountCodePoint(text, 0xFF5E) + CountCodePoint(text, 0x301C);
}

int CountAsciiTilde(absl::string_view text) {
  return CountCodePoint(text, 0x007E);
}

std::string FirstJapaneseWaveDashBytes(absl::string_view text) {
  const std::vector<Utf8CharForSymbolRestore> chars =
      SplitUtf8ForSymbolRestore(text);
  for (const Utf8CharForSymbolRestore& ch : chars) {
    if (IsJapaneseWaveDash(ch.cp)) {
      return ch.bytes;
    }
  }
  return {};
}

}  // namespace

std::string ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
    absl::string_view key,
    absl::string_view mozc_value,
    absl::string_view zenz_value) {
  const int key_wave_dash_count = CountJapaneseWaveDashFamily(key);
  const int mozc_wave_dash_count = CountJapaneseWaveDashFamily(mozc_value);

  // Prefer `key` because the caller passes raw-preedit-derived text here for
  // live correction.  `mozc_value` may already contain an ASCII tilde due to
  // converter or feedback influence, and must not cancel restoration of the
  // user-entered Japanese wave dash style.
  absl::string_view style_source;
  if (key_wave_dash_count > 0) {
    style_source = key;
  } else if (mozc_wave_dash_count > 0) {
    style_source = mozc_value;
  } else {
    return std::string(zenz_value);
  }

  const int source_wave_dash_count =
      CountJapaneseWaveDashFamily(style_source);
  const int zenz_wave_dash_count = CountJapaneseWaveDashFamily(zenz_value);
  const int missing_wave_dash_count =
      source_wave_dash_count - zenz_wave_dash_count;
  if (missing_wave_dash_count <= 0) {
    return std::string(zenz_value);
  }

  const int source_ascii_tilde_count = CountAsciiTilde(style_source);
  const int zenz_ascii_tilde_count = CountAsciiTilde(zenz_value);
  int replacement_budget = std::min(
      missing_wave_dash_count, zenz_ascii_tilde_count - source_ascii_tilde_count);
  if (replacement_budget <= 0) {
    return std::string(zenz_value);
  }

  const std::vector<Utf8CharForSymbolRestore> source_chars =
      SplitUtf8ForSymbolRestore(style_source);
  const std::vector<Utf8CharForSymbolRestore> zenz_chars =
      SplitUtf8ForSymbolRestore(zenz_value);
  if (source_chars.empty() || zenz_chars.empty()) {
    return std::string(zenz_value);
  }

  const std::string default_replacement =
      FirstJapaneseWaveDashBytes(style_source);
  if (default_replacement.empty()) {
    return std::string(zenz_value);
  }

  std::string restored;
  restored.reserve(zenz_value.size());
  for (size_t i = 0; i < zenz_chars.size(); ++i) {
    const Utf8CharForSymbolRestore& ch = zenz_chars[i];
    const bool positional_restore =
        i < source_chars.size() && IsJapaneseWaveDash(source_chars[i].cp);

    // If the source did not contain ASCII tilde at all, any extra ASCII tilde
    // returned by Zenz is highly likely to be a normalized Japanese wave dash.
    // If the source did contain ASCII tilde, avoid broad fallback replacement
    // and only restore exact character positions to preserve intentional ASCII.
    const bool safe_fallback_restore = source_ascii_tilde_count == 0;

    if (replacement_budget > 0 && IsAsciiTilde(ch.cp) &&
        (positional_restore || safe_fallback_restore)) {
      restored.append(positional_restore ? source_chars[i].bytes
                                         : default_replacement);
      --replacement_budget;
      continue;
    }

    restored.append(ch.bytes);
  }

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
