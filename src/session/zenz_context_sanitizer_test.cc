// Copyright 2010-2026, Google Inc.
// All rights reserved.

#include "session/zenz_context_sanitizer.h"

#include <string>

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzContextSanitizerTest,
     EmptyContextAndZeroLimitAreEmpty) {
  const ZenzContextSanitizer sanitizer;

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("", 24);

    EXPECT_TRUE(result.sanitized_context.empty());
    EXPECT_EQ(result.context_class, "empty");
    EXPECT_FALSE(result.allowed_for_prompt);
    EXPECT_FALSE(result.allowed_for_learning);
    EXPECT_EQ(result.reason, "empty_context");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("日本語", 0);

    EXPECT_TRUE(result.sanitized_context.empty());
    EXPECT_EQ(result.context_class, "empty");
    EXPECT_FALSE(result.allowed_for_prompt);
    EXPECT_FALSE(result.allowed_for_learning);
    EXPECT_EQ(result.reason, "empty_context");
  }
}

TEST(ZenzContextSanitizerTest,
     TruncatesFromTheLeftByUnicodeCharacters) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("甲乙丙丁", 2);

  EXPECT_EQ(result.sanitized_context, "丙丁");
  EXPECT_EQ(result.context_class, "japanese_only");
  EXPECT_TRUE(result.allowed_for_prompt);
  EXPECT_FALSE(result.allowed_for_learning);
  EXPECT_EQ(result.reason, "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     PreservesNewlinesBeforePromptConstruction) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "前文\n現在",
          24);

  EXPECT_EQ(
      result.sanitized_context,
      "前文\n現在");
  EXPECT_EQ(
      result.context_class,
      "japanese_only");
  EXPECT_TRUE(result.allowed_for_prompt);
  EXPECT_FALSE(result.allowed_for_learning);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     PreservesLegacyJapaneseContextClasses) {
  const ZenzContextSanitizer sanitizer;

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("今日は", 24);

    EXPECT_EQ(
        result.context_class,
        "japanese_only");
    EXPECT_EQ(
        result.sanitized_context,
        "今日は");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(
        result.reason,
        "context_allowed");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("今日は?", 24);

    EXPECT_EQ(
        result.context_class,
        "japanese_with_punctuation");
    EXPECT_EQ(
        result.sanitized_context,
        "今日は?");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(
        result.reason,
        "context_allowed");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("今日はA", 24);

    EXPECT_EQ(
        result.context_class,
        "mixed_japanese_ascii");
    EXPECT_EQ(
        result.sanitized_context,
        "今日はA");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(
        result.reason,
        "context_allowed");
  }
}

TEST(ZenzContextSanitizerTest,
     EmojiOnlyNoLongerCountsAsJapaneseContext) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("😀", 24);

  EXPECT_EQ(
      result.context_class,
      "symbol_or_other");
  EXPECT_TRUE(
      result.sanitized_context.empty());
  EXPECT_FALSE(
      result.allowed_for_prompt);
  EXPECT_FALSE(
      result.allowed_for_learning);
  EXPECT_EQ(
      result.reason,
      "non_japanese_context_rejected");
}

TEST(ZenzContextSanitizerTest,
     FullWidthLatinOnlyNoLongerCountsAsJapaneseContext) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("ＡＢＣ", 24);

  EXPECT_EQ(
      result.context_class,
      "ascii_or_digit");
  EXPECT_TRUE(
      result.sanitized_context.empty());
  EXPECT_FALSE(
      result.allowed_for_prompt);
  EXPECT_EQ(
      result.reason,
      "non_japanese_context_rejected");
}

TEST(ZenzContextSanitizerTest,
     HanOnlyContextRemainsAvailable) {
  const ZenzContextSanitizer sanitizer;

  // Han ideographs are shared by Japanese and Chinese. This layer uses
  // Unicode script evidence, not language identification.
  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("中文", 24);

  EXPECT_EQ(
      result.context_class,
      "japanese_only");
  EXPECT_EQ(
      result.sanitized_context,
      "中文");
  EXPECT_TRUE(
      result.allowed_for_prompt);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     JapaneseContextMayContainEmojiWithoutEmojiBecomingLanguageEvidence) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "今日は😀",
          24);

  EXPECT_EQ(
      result.context_class,
      "japanese_only");
  EXPECT_EQ(
      result.sanitized_context,
      "今日は😀");
  EXPECT_TRUE(
      result.allowed_for_prompt);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     EmojiCannotInflateJapaneseDominanceAgainstAscii) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "日😀????",
          24);

  EXPECT_EQ(
      result.context_class,
      "japanese_with_punctuation");
  EXPECT_TRUE(
      result.sanitized_context.empty());
  EXPECT_FALSE(
      result.allowed_for_prompt);
  EXPECT_EQ(
      result.reason,
      "non_japanese_context_rejected");
}

TEST(ZenzContextSanitizerTest,
     RefinedPrivacyDoesNotRejectEightVisibleAsciiByLengthAlone) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "日本語!!!!!!!!",
          24);

  EXPECT_EQ(
      result.context_class,
      "japanese_with_punctuation");
  EXPECT_EQ(
      result.sanitized_context,
      "日本語!!!!!!!!");
  EXPECT_TRUE(
      result.allowed_for_prompt);
  EXPECT_FALSE(
      result.allowed_for_learning);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     RefinedPrivacyDoesNotRejectFourDigitsByLengthAlone) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "日本語1234",
          24);

  EXPECT_EQ(
      result.context_class,
      "mixed_japanese_ascii");
  EXPECT_EQ(
      result.sanitized_context,
      "日本語1234");
  EXPECT_TRUE(
      result.allowed_for_prompt);
  EXPECT_FALSE(
      result.allowed_for_learning);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     RejectsCurrentSensitiveAsciiPatterns) {
  const ZenzContextSanitizer sanitizer;

  const char* const inputs[] = {
      "https://example.com",
      "foo@example.com",
      "C:\\Users\\Makoto\\notes.txt",
      "/home/user/notes.txt",
      "password=abc",
      "token=abc",
      "secret=abc",
      "sk-abc",
      "ghp_abc",
      "xoxb-abc",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz(
            input,
            128);

    EXPECT_EQ(
        result.context_class,
        "sensitive_like");
    EXPECT_TRUE(
        result.sanitized_context.empty());
    EXPECT_FALSE(
        result.allowed_for_prompt);
    EXPECT_FALSE(
        result.allowed_for_learning);
    EXPECT_EQ(
        result.reason,
        "sensitive_context_rejected");
  }
}

TEST(ZenzContextSanitizerTest,
     RefinedPrivacyRejectsStructuredSensitiveContext) {
  const ZenzContextSanitizer sanitizer;

  const char* const inputs[] = {
      "a@b",
      "example.com",
      "foo\\bar",
      "Bearer abcdefghijklmnopqrstuvwxyz",
      "pk_abc",
      "12345678",
      "パスワードを変更",
      "認証コードを入力",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz(
            input,
            128);

    EXPECT_EQ(
        result.context_class,
        "sensitive_like");
    EXPECT_TRUE(
        result.sanitized_context.empty());
    EXPECT_FALSE(
        result.allowed_for_prompt);
    EXPECT_FALSE(
        result.allowed_for_learning);
    EXPECT_EQ(
        result.reason,
        "sensitive_context_rejected");
  }
}

TEST(ZenzContextSanitizerTest,
     MixedScriptPolicyAcceptsNaturalJapaneseTechnicalContext) {
  const ZenzContextSanitizer sanitizer;

  const char* const inputs[] = {
      "2026年",
      "Windows11を使う",
      "GPT-5で生成する",
      "UTF-8に変換する",
      "HTTP/2に対応する",
      "M1で動かす",
      "v3.2へ更新する",
      "C++で書く",
      "macOS15でも使える",
      "Visual Studioを使う",
      "Visual Studio Codeで書く",
      "OpenAI APIを使う",
      "GitHub Actionsで実行する",
      "Ruby on Railsを使う",
      "Windows Subsystem for Linuxを使う",
      "「Windows11」を使う",
      "「Visual Studio」を使う",
      "（OpenAI API）を使う",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz(
            input,
            128);

    EXPECT_EQ(
        result.context_class,
        "mixed_japanese_ascii");
    EXPECT_EQ(
        result.sanitized_context,
        input);
    EXPECT_TRUE(
        result.allowed_for_prompt);
    EXPECT_FALSE(
        result.allowed_for_learning);
    EXPECT_EQ(
        result.reason,
        "context_allowed");
  }
}

TEST(ZenzContextSanitizerTest,
     MixedScriptPolicyStillRejectsNonJapaneseOrUnattachedContext) {
  const ZenzContextSanitizer sanitizer;

  const char* const inputs[] = {
      "Windows11",
      "2026",
      "日本語 ABCDE",
      "This is Englishです",
      "hello worldを",
      "ABCDEFG日",
      "日한국어문",
      "日😀😀😀😀",
      "ＡＢＣＤＥを",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz(
            input,
            128);

    EXPECT_TRUE(
        result.sanitized_context.empty());
    EXPECT_FALSE(
        result.allowed_for_prompt);
    EXPECT_FALSE(
        result.allowed_for_learning);
    EXPECT_EQ(
        result.reason,
        "non_japanese_context_rejected");
  }
}

TEST(ZenzContextSanitizerTest,
     PrivacyGateStillPrecedesMixedScriptPolicy) {
  const ZenzContextSanitizer sanitizer;

  const char* const inputs[] = {
      "passwordを変更",
      "パスワードを変更",
      "認証コードを入力",
      "a@bを使う",
      "https://example.comを開く",
      "foo\\barを開く",
      "ghp_abcを使う",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz(
            input,
            128);

    EXPECT_EQ(
        result.context_class,
        "sensitive_like");
    EXPECT_TRUE(
        result.sanitized_context.empty());
    EXPECT_FALSE(
        result.allowed_for_prompt);
    EXPECT_FALSE(
        result.allowed_for_learning);
    EXPECT_EQ(
        result.reason,
        "sensitive_context_rejected");
  }
}

TEST(ZenzContextSanitizerTest,
     RejectsAsciiOnlyContextAsNonJapanese) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "abc",
          24);

  EXPECT_EQ(
      result.context_class,
      "ascii_or_digit");
  EXPECT_TRUE(
      result.sanitized_context.empty());
  EXPECT_FALSE(
      result.allowed_for_prompt);
  EXPECT_FALSE(
      result.allowed_for_learning);
  EXPECT_EQ(
      result.reason,
      "non_japanese_context_rejected");
}

TEST(ZenzContextSanitizerTest,
     CurrentTruncationOccursBeforeSensitivePatternClassification) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "token=abcdef 日本語",
          3);

  EXPECT_EQ(
      result.context_class,
      "japanese_only");
  EXPECT_EQ(
      result.sanitized_context,
      "日本語");
  EXPECT_TRUE(
      result.allowed_for_prompt);
  EXPECT_FALSE(
      result.allowed_for_learning);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     RejectsContextDominatedByUnknownNonJapaneseScript) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "日한국어문",
          24);

  EXPECT_TRUE(
      result.sanitized_context.empty());
  EXPECT_FALSE(
      result.allowed_for_prompt);
  EXPECT_FALSE(
      result.allowed_for_learning);
  EXPECT_EQ(
      result.reason,
      "non_japanese_context_rejected");
}

TEST(ZenzContextSanitizerTest,
     KeepsShortJapaneseContextWithNormalPunctuation) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "「日。」",
          24);

  EXPECT_EQ(
      result.sanitized_context,
      "「日。」");
  EXPECT_TRUE(
      result.allowed_for_prompt);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     KeepsNormalJapaneseContextWithEmoji) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "今日は😀😀",
          24);

  EXPECT_EQ(
      result.sanitized_context,
      "今日は😀😀");
  EXPECT_TRUE(
      result.allowed_for_prompt);
  EXPECT_EQ(
      result.reason,
      "context_allowed");
}

TEST(ZenzContextSanitizerTest,
     RejectsEmojiDominatedContextWithMinimalJapaneseSignal) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz(
          "日😀😀😀😀",
          24);

  EXPECT_TRUE(
      result.sanitized_context.empty());
  EXPECT_FALSE(
      result.allowed_for_prompt);
  EXPECT_EQ(
      result.reason,
      "non_japanese_context_rejected");
}
}  // namespace
}  // namespace session
}  // namespace mozc