#include "session/zenz_text_privacy_analyzer.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {
namespace {

bool StartsWithString(
    absl::string_view text,
    absl::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

bool IsAsciiAlpha(unsigned char c) {
  return ('a' <= c && c <= 'z') ||
         ('A' <= c && c <= 'Z');
}

bool IsAsciiDigit(unsigned char c) {
  return '0' <= c && c <= '9';
}

bool IsAsciiAlnum(unsigned char c) {
  return IsAsciiAlpha(c) ||
         IsAsciiDigit(c);
}

bool IsAsciiVisible(unsigned char c) {
  return 0x21 <= c && c <= 0x7e;
}

bool IsAsciiHexDigit(unsigned char c) {
  return IsAsciiDigit(c) ||
         ('a' <= c && c <= 'f') ||
         ('A' <= c && c <= 'F');
}

std::string ToLowerAscii(
    absl::string_view text) {
  std::string result;
  result.reserve(text.size());

  for (const unsigned char c : text) {
    if ('A' <= c && c <= 'Z') {
      result.push_back(
          static_cast<char>(
              c - 'A' + 'a'));
    } else {
      result.push_back(
          static_cast<char>(c));
    }
  }

  return result;
}

bool ContainsAsciiSubstring(
    const std::string& text,
    absl::string_view needle) {
  return text.find(
             std::string(needle)) !=
         std::string::npos;
}

// ============================================================
// Existing live key/value privacy semantics.
// ============================================================

bool LooksLikeLiveUrlOrDomain(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  if (lower.find("://") !=
          std::string::npos ||
      lower.find("www.") !=
          std::string::npos) {
    return true;
  }

  constexpr absl::string_view
      kDomainSuffixes[] = {
          ".com",
          ".net",
          ".org",
          ".jp",
          ".co.jp",
          ".io",
          ".dev",
          ".app",
          ".local",
          ".localhost",
      };

  for (const absl::string_view suffix :
       kDomainSuffixes) {
    if (ContainsAsciiSubstring(
            lower,
            suffix)) {
      return true;
    }
  }

  return false;
}

bool LooksLikeLiveEmail(
    absl::string_view text) {
  // Preserve the existing broad live policy. In Japanese composition,
  // raw '@' is treated as address-like or handle-like content.
  return text.find('@') !=
         absl::string_view::npos;
}

bool LooksLikeLivePath(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  if (text.find('\\') !=
      absl::string_view::npos) {
    return true;
  }

  if (lower.size() >= 3 &&
      IsAsciiAlpha(
          static_cast<unsigned char>(
              lower[0])) &&
      lower[1] == ':' &&
      (lower[2] == '\\' ||
       lower[2] == '/')) {
    return true;
  }

  if (StartsWithString(lower, "/") ||
      StartsWithString(lower, "~/") ||
      lower.find("../") !=
          std::string::npos ||
      lower.find("./") !=
          std::string::npos) {
    return true;
  }

  return false;
}

bool IsAsciiTokenChar(
    unsigned char c) {
  return IsAsciiAlnum(c) ||
         c == '_' ||
         c == '-' ||
         c == '.';
}

bool IsIpv4LikeAsciiToken(
    absl::string_view token) {
  size_t i = 0;
  int group_count = 0;

  while (i < token.size()) {
    if (group_count >= 4) {
      return false;
    }

    const size_t start = i;
    int value = 0;

    while (i < token.size() &&
           IsAsciiDigit(
               static_cast<unsigned char>(
                   token[i]))) {
      value =
          value * 10 +
          (token[i] - '0');

      if (value > 255) {
        return false;
      }

      ++i;
    }

    if (i == start) {
      return false;
    }

    ++group_count;

    if (group_count == 4) {
      break;
    }

    if (i >= token.size() ||
        token[i] != '.') {
      return false;
    }

    ++i;
  }

  return group_count == 4 &&
         i == token.size();
}

bool IsVersionLikeAsciiToken(
    absl::string_view token) {
  if (token.empty()) {
    return false;
  }

  size_t i = 0;

  if (i < token.size() &&
      (token[i] == 'v' ||
       token[i] == 'V')) {
    ++i;
  }

  const auto consume_digits =
      [&token, &i]() {
        const size_t start = i;

        while (i < token.size() &&
               IsAsciiDigit(
                   static_cast<unsigned char>(
                       token[i]))) {
          ++i;
        }

        return i > start;
      };

  if (!consume_digits()) {
    return false;
  }

  size_t numeric_group_count = 1;
  bool saw_dot = false;

  while (i < token.size() &&
         token[i] == '.') {
    saw_dot = true;
    ++i;

    if (!consume_digits()) {
      return false;
    }

    ++numeric_group_count;

    if (numeric_group_count > 4) {
      return false;
    }
  }

  // Require at least one dot. A plain v12345678 is not treated as a harmless
  // version string.
  if (!saw_dot) {
    return false;
  }

  if (i < token.size() &&
      token[i] == '-') {
    ++i;

    const size_t suffix_start = i;

    while (i < token.size() &&
           IsAsciiAlpha(
               static_cast<unsigned char>(
                   token[i]))) {
      ++i;
    }

    if (i == suffix_start) {
      return false;
    }

    while (i < token.size() &&
           IsAsciiDigit(
               static_cast<unsigned char>(
                   token[i]))) {
      ++i;
    }
  }

  return i == token.size();
}

bool LooksLikeLongAsciiToken(
    absl::string_view text) {
  size_t token_start = 0;
  bool in_token = false;

  const auto check_token =
      [&text](size_t start,
              size_t end) {
        if (end <= start) {
          return false;
        }

        const absl::string_view token =
            text.substr(
                start,
                end - start);

        if (IsIpv4LikeAsciiToken(
                token)) {
          return true;
        }

        if (IsVersionLikeAsciiToken(
                token)) {
          return false;
        }

        size_t longest_digit_run = 0;
        size_t current_digit_run = 0;

        bool has_alpha = false;
        bool has_digit = false;
        bool has_symbol = false;
        bool all_hex = true;

        for (const unsigned char c :
             token) {
          if (IsAsciiDigit(c)) {
            has_digit = true;
            ++current_digit_run;

            longest_digit_run =
                std::max(
                    longest_digit_run,
                    current_digit_run);
          } else {
            current_digit_run = 0;
          }

          if (IsAsciiAlpha(c)) {
            has_alpha = true;
          }

          if (c == '_' ||
              c == '-' ||
              c == '.') {
            has_symbol = true;
          }

          if (!IsAsciiHexDigit(c)) {
            all_hex = false;
          }
        }

        const size_t len =
            token.size();

        if (longest_digit_run >= 8) {
          return true;
        }

        if (len >= 12 &&
            has_alpha &&
            has_digit &&
            has_symbol) {
          return true;
        }

        if (len >= 16 &&
            has_alpha &&
            has_digit) {
          return true;
        }

        if (len >= 16 &&
            all_hex &&
            has_alpha) {
          return true;
        }

        if (len >= 24 &&
            has_alpha) {
          return true;
        }

        if (len >= 32) {
          return true;
        }

        return false;
      };

  for (size_t i = 0;
       i < text.size();
       ++i) {
    const unsigned char c =
        static_cast<unsigned char>(
            text[i]);

    if (IsAsciiTokenChar(c)) {
      if (!in_token) {
        token_start = i;
        in_token = true;
      }

      continue;
    }

    if (in_token) {
      if (check_token(
              token_start,
              i)) {
        return true;
      }

      in_token = false;
    }
  }

  if (in_token &&
      check_token(
          token_start,
          text.size())) {
    return true;
  }

  return false;
}

bool LooksLikeLiveKnownSecretPrefix(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  constexpr absl::string_view
      kPrefixes[] = {
          "ghp_",
          "github_pat_",
          "glpat-",
          "sk-",
          "xoxb-",
          "xoxp-",
          "ya29.",
          "akia",
          "bearer ",
      };

  for (const absl::string_view prefix :
       kPrefixes) {
    if (ContainsAsciiSubstring(
            lower,
            prefix)) {
      return true;
    }
  }

  return false;
}

bool ContainsLiveSensitiveCredentialWord(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  constexpr absl::string_view
      kAsciiWords[] = {
          "password",
          "passwd",
          "passphrase",
          "secret",
          "token",
          "apikey",
          "api_key",
          "credential",
          "privatekey",
          "private_key",
          "authorization",
      };

  for (const absl::string_view word :
       kAsciiWords) {
    if (ContainsAsciiSubstring(
            lower,
            word)) {
      return true;
    }
  }

  constexpr absl::string_view
      kJapaneseWords[] = {
          "パスワード",
          "暗証番号",
          "認証コード",
          "認証番号",
          "秘密鍵",
          "秘密キー",
          "トークン",
          "アクセストークン",
          "APIキー",
          "apiキー",
      };

  for (const absl::string_view word :
       kJapaneseWords) {
    if (text.find(word) !=
        absl::string_view::npos) {
      return true;
    }
  }

  return false;
}

// ============================================================
// Historical context privacy semantics retained for regression comparison.
//
// These deliberately remain less precise than the refined kContext policy.
// ============================================================

bool ContainsLegacyContextCredentialWord(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  constexpr absl::string_view
      kWords[] = {
          "password",
          "passwd",
          "pwd",
          "token",
          "secret",
          "apikey",
          "api_key",
          "authorization",
          "bearer ",
          "cookie",
          "sessionid",
          "session_id",
      };

  for (const absl::string_view word :
       kWords) {
    if (ContainsAsciiSubstring(
            lower,
            word)) {
      return true;
    }
  }

  return false;
}

bool LooksLikeLegacyContextUrl(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  return ContainsAsciiSubstring(
             lower,
             "http://") ||
         ContainsAsciiSubstring(
             lower,
             "https://") ||
         ContainsAsciiSubstring(
             lower,
             "www.") ||
         ContainsAsciiSubstring(
             lower,
             "mailto:");
}

bool LooksLikeLegacyContextEmail(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  return ContainsAsciiSubstring(
             lower,
             "@") &&
         ContainsAsciiSubstring(
             lower,
             ".");
}

bool LooksLikeLegacyContextPath(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  return ContainsAsciiSubstring(
             lower,
             "c:\\") ||
         ContainsAsciiSubstring(
             lower,
             "\\users\\") ||
         ContainsAsciiSubstring(
             lower,
             "/home/") ||
         ContainsAsciiSubstring(
             lower,
             "/users/");
}

bool LooksLikeLegacyContextSecretPrefix(
    absl::string_view text) {
  const std::string lower =
      ToLowerAscii(text);

  return ContainsAsciiSubstring(
             lower,
             "sk-") ||
         ContainsAsciiSubstring(
             lower,
             "pk_") ||
         ContainsAsciiSubstring(
             lower,
             "ghp_") ||
         ContainsAsciiSubstring(
             lower,
             "xoxb-");
}

bool ContainsLegacyLongVisibleAsciiRun(
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

bool ContainsLegacyLongDigitRun(
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

ZenzTextPrivacyAnalysis AnalyzeLiveText(
    absl::string_view text) {
  if (LooksLikeLiveEmail(text)) {
    return {
        ZenzTextPrivacySignal::
            kEmailLike};
  }

  if (LooksLikeLiveUrlOrDomain(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kUrlOrDomainLike};
  }

  if (LooksLikeLivePath(text)) {
    return {
        ZenzTextPrivacySignal::
            kPathLike};
  }

  if (LooksLikeLiveKnownSecretPrefix(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kSecretPrefix};
  }

  if (LooksLikeLongAsciiToken(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kTokenLike};
  }

  if (ContainsLiveSensitiveCredentialWord(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kCredentialWord};
  }

  return {};
}

bool ContainsContextSensitiveCredentialWord(
    absl::string_view text) {
  // Preserve every credential word recognized by either historical context
  // privacy or the more complete live policy.
  return ContainsLegacyContextCredentialWord(text) ||
         ContainsLiveSensitiveCredentialWord(text);
}

bool LooksLikeContextUrlOrDomain(
    absl::string_view text) {
  if (LooksLikeLiveUrlOrDomain(text)) {
    return true;
  }

  // The historical context policy additionally treated mailto: as sensitive
  // even when no '@' was present.
  const std::string lower =
      ToLowerAscii(text);

  return ContainsAsciiSubstring(
      lower,
      "mailto:");
}

bool LooksLikeContextKnownSecretPrefix(
    absl::string_view text) {
  if (LooksLikeLiveKnownSecretPrefix(text)) {
    return true;
  }

  // Preserve the historical pk_ prefix without changing kLiveText semantics.
  const std::string lower =
      ToLowerAscii(text);

  return ContainsAsciiSubstring(
      lower,
      "pk_");
}

ZenzTextPrivacyAnalysis AnalyzeContextText(
    absl::string_view text) {
  // Prefer concrete secret/token schemes before generic credential
  // vocabulary. Some strings, notably "Bearer ...", intentionally match both.
  // Keeping the prefix signal first also matches the already validated live
  // privacy precedence.
  if (LooksLikeContextKnownSecretPrefix(text)) {
    return {
        ZenzTextPrivacySignal::kSecretPrefix};
  }

  if (ContainsContextSensitiveCredentialWord(text)) {
    return {
        ZenzTextPrivacySignal::kCredentialWord};
  }

  // Context privacy deliberately treats any '@' as potentially identifying.
  // This is stronger than the historical '@' + '.' heuristic and matches the
  // already validated live privacy boundary.
  if (LooksLikeLiveEmail(text)) {
    return {
        ZenzTextPrivacySignal::kEmailLike};
  }

  if (LooksLikeContextUrlOrDomain(text)) {
    return {
        ZenzTextPrivacySignal::kUrlOrDomainLike};
  }

  // Use the broader live path detector: absolute paths, relative paths and
  // backslash-bearing Windows-like text can expose local machine structure.
  if (LooksLikeLivePath(text)) {
    return {
        ZenzTextPrivacySignal::kPathLike};
  }

  // Keep the existing conservative opaque-token detector. Unlike the legacy
  // context rule, this does not reject every four-digit year or every
  // eight-character ASCII word.
  if (LooksLikeLongAsciiToken(text)) {
    return {
        ZenzTextPrivacySignal::kTokenLike};
  }

  return {};
}
ZenzTextPrivacyAnalysis AnalyzeLegacyContext(
    absl::string_view text) {
  if (ContainsLegacyContextCredentialWord(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kCredentialWord};
  }

  if (LooksLikeLegacyContextUrl(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kUrlOrDomainLike};
  }

  if (LooksLikeLegacyContextEmail(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kEmailLike};
  }

  if (LooksLikeLegacyContextPath(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kPathLike};
  }

  if (LooksLikeLegacyContextSecretPrefix(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kSecretPrefix};
  }

  if (ContainsLegacyLongVisibleAsciiRun(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kLegacyLongVisibleAscii};
  }

  if (ContainsLegacyLongDigitRun(
          text)) {
    return {
        ZenzTextPrivacySignal::
            kLegacyLongDigitRun};
  }

  return {};
}

}  // namespace

const char*
ZenzTextPrivacyAnalysis::reason() const {
  switch (signal) {
    case ZenzTextPrivacySignal::kNone:
      return "allow";

    case ZenzTextPrivacySignal::
        kEmailLike:
      return "email_like";

    case ZenzTextPrivacySignal::
        kUrlOrDomainLike:
      return "url_or_domain_like";

    case ZenzTextPrivacySignal::
        kPathLike:
      return "path_like";

    case ZenzTextPrivacySignal::
        kSecretPrefix:
      return "secret_prefix";

    case ZenzTextPrivacySignal::
        kTokenLike:
      return "token_like";

    case ZenzTextPrivacySignal::
        kCredentialWord:
      return "credential_word";

    case ZenzTextPrivacySignal::
        kLegacyLongVisibleAscii:
      return "legacy_long_visible_ascii";

    case ZenzTextPrivacySignal::
        kLegacyLongDigitRun:
      return "legacy_long_digit_run";
  }

  return "unspecified";
}

ZenzTextPrivacyAnalysis
ZenzTextPrivacyAnalyzer::Analyze(
    const absl::string_view text,
    const ZenzTextPrivacyPolicy policy) const {
  switch (policy) {
    case ZenzTextPrivacyPolicy::
        kLiveText:
      return AnalyzeLiveText(text);

    case ZenzTextPrivacyPolicy::
        kLegacyContext:
      return AnalyzeLegacyContext(text);

    case ZenzTextPrivacyPolicy::
        kContext:
      return AnalyzeContextText(text);
  }

  return {};
}

}  // namespace session
}  // namespace mozc
