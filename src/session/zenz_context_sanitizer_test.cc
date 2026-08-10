// Copyright 2010-2026, Google Inc.
// All rights reserved.

#include "session/zenz_context_sanitizer.h"

#include <string>

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzContextSanitizerTest, EmptyContextAndZeroLimitAreEmpty) {
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

TEST(ZenzContextSanitizerTest, TruncatesFromTheLeftByUnicodeCharacters) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("甲乙丙丁", 2);

  EXPECT_EQ(result.sanitized_context, "丙丁");
  EXPECT_EQ(result.context_class, "japanese_only");
  EXPECT_TRUE(result.allowed_for_prompt);
  EXPECT_FALSE(result.allowed_for_learning);
  EXPECT_EQ(result.reason, "context_allowed");
}

TEST(ZenzContextSanitizerTest, PreservesNewlinesBeforePromptConstruction) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("前文\n現在", 24);

  EXPECT_EQ(result.sanitized_context, "前文\n現在");
  EXPECT_EQ(result.context_class, "japanese_only");
  EXPECT_TRUE(result.allowed_for_prompt);
  EXPECT_FALSE(result.allowed_for_learning);
  EXPECT_EQ(result.reason, "context_allowed");
}

TEST(ZenzContextSanitizerTest, CharacterizesJapaneseContextClasses) {
  const ZenzContextSanitizer sanitizer;

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("今日は", 24);

    EXPECT_EQ(result.context_class, "japanese_only");
    EXPECT_EQ(result.sanitized_context, "今日は");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "context_allowed");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("今日は?", 24);

    EXPECT_EQ(result.context_class, "japanese_with_punctuation");
    EXPECT_EQ(result.sanitized_context, "今日は?");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "context_allowed");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("今日はA", 24);

    EXPECT_EQ(result.context_class, "mixed_japanese_ascii");
    EXPECT_EQ(result.sanitized_context, "今日はA");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "context_allowed");
  }
}

TEST(ZenzContextSanitizerTest,
     CurrentClassifierTreatsEmojiAndCjkAsJapaneseLike) {
  const ZenzContextSanitizer sanitizer;

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("😀", 24);

    EXPECT_EQ(result.context_class, "japanese_only");
    EXPECT_EQ(result.sanitized_context, "😀");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "context_allowed");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("中文", 24);

    EXPECT_EQ(result.context_class, "japanese_only");
    EXPECT_EQ(result.sanitized_context, "中文");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "context_allowed");
  }
}

TEST(ZenzContextSanitizerTest,
     CurrentPrivacyRuleRejectsEightConsecutiveVisibleAsciiCharacters) {
  const ZenzContextSanitizer sanitizer;

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("日本語!!!!!!!", 24);

    EXPECT_EQ(result.context_class, "japanese_with_punctuation");
    EXPECT_EQ(result.sanitized_context, "日本語!!!!!!!");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "context_allowed");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("日本語!!!!!!!!", 24);

    EXPECT_EQ(result.context_class, "sensitive_like");
    EXPECT_TRUE(result.sanitized_context.empty());
    EXPECT_FALSE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "sensitive_context_rejected");
  }
}

TEST(ZenzContextSanitizerTest,
     CurrentPrivacyRuleRejectsFourConsecutiveAsciiDigits) {
  const ZenzContextSanitizer sanitizer;

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("日本語123", 24);

    EXPECT_EQ(result.context_class, "mixed_japanese_ascii");
    EXPECT_EQ(result.sanitized_context, "日本語123");
    EXPECT_TRUE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "context_allowed");
  }

  {
    const ZenzContextSanitizationResult result =
        sanitizer.SanitizeForZenz("日本語1234", 24);

    EXPECT_EQ(result.context_class, "sensitive_like");
    EXPECT_TRUE(result.sanitized_context.empty());
    EXPECT_FALSE(result.allowed_for_prompt);
    EXPECT_EQ(result.reason, "sensitive_context_rejected");
  }
}

TEST(ZenzContextSanitizerTest, RejectsCurrentSensitiveAsciiPatterns) {
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
        sanitizer.SanitizeForZenz(input, 128);

    EXPECT_EQ(result.context_class, "sensitive_like");
    EXPECT_TRUE(result.sanitized_context.empty());
    EXPECT_FALSE(result.allowed_for_prompt);
    EXPECT_FALSE(result.allowed_for_learning);
    EXPECT_EQ(result.reason, "sensitive_context_rejected");
  }
}

TEST(ZenzContextSanitizerTest, RejectsAsciiOnlyContextAsNonJapanese) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("abc", 24);

  EXPECT_EQ(result.context_class, "ascii_or_digit");
  EXPECT_TRUE(result.sanitized_context.empty());
  EXPECT_FALSE(result.allowed_for_prompt);
  EXPECT_FALSE(result.allowed_for_learning);
  EXPECT_EQ(result.reason, "non_japanese_context_rejected");
}

TEST(ZenzContextSanitizerTest,
     CurrentTruncationOccursBeforeSensitivePatternClassification) {
  const ZenzContextSanitizer sanitizer;

  const ZenzContextSanitizationResult result =
      sanitizer.SanitizeForZenz("token=abcdef 日本語", 3);

  EXPECT_EQ(result.context_class, "japanese_only");
  EXPECT_EQ(result.sanitized_context, "日本語");
  EXPECT_TRUE(result.allowed_for_prompt);
  EXPECT_FALSE(result.allowed_for_learning);
  EXPECT_EQ(result.reason, "context_allowed");
}

}  // namespace
}  // namespace session
}  // namespace mozc