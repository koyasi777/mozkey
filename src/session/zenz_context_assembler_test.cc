#include "session/zenz_context_assembler.h"

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzContextAssemblerTest,
     PreservesDirectionalCharacterBudgets) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "甲乙丙丁";
  input.following_text = "甲乙丙丁";
  input.left_max_chars = 2;
  input.right_max_chars = 2;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "丙丁");

  EXPECT_EQ(
      result.right.prompt_context,
      "甲乙");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);

  EXPECT_TRUE(
      result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     LeftCrossesSingleLineBreakWithinParagraph) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "前の行\n現在の文脈";
  input.left_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "前の行\n現在の文脈");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);

  EXPECT_EQ(
      result.left.reason,
      "context_allowed");
}

TEST(ZenzContextAssemblerTest,
     LeftCrossesSingleCrLfWithinParagraph) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "前の行\r\n現在の文脈";
  input.left_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "前の行\r\n現在の文脈");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     LeftStopsAtBlankLine) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "前の段落\n\n現在の文脈";
  input.left_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "現在の文脈");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     LeftStopsAtWhitespaceOnlyBlankLine) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "前の段落\n \t\n現在の文脈";
  input.left_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "現在の文脈");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     LeftKeepsPrecedingSentenceInsideCurrentParagraph) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "方式Aを提案した。その方法では";
  input.left_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "方式Aを提案した。その方法では");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     LeftBudgetIsAppliedAfterParagraphSelection) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "前の段落\n\n甲乙丙丁";
  input.left_max_chars = 2;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "丙丁");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightStopsAtFirstSentenceBoundary) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "を採用する。次の議題に移る";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.right.prompt_context,
      "を採用する。");

  EXPECT_TRUE(
      result.right.allowed_for_prompt);

  EXPECT_EQ(
      result.right.reason,
      "context_allowed");
}

TEST(ZenzContextAssemblerTest,
     RightKeepsRepeatedTerminatorsAndClosingQuote) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "だった！？」次の文";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.right.prompt_context,
      "だった！？」");

  EXPECT_TRUE(
      result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightCrossesSingleLineBreakWithinContinuation) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "を採用し\n継続する。次の文";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.right.prompt_context,
      "を採用し\n継続する。");

  EXPECT_TRUE(
      result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightCrossesSingleCrLfWithinContinuation) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "を採用し\r\n継続する。次の文";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.right.prompt_context,
      "を採用し\r\n継続する。");

  EXPECT_TRUE(
      result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightStopsBeforeBlankLine) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "を採用する\n\n次の段落";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.right.prompt_context,
      "を採用する");

  EXPECT_TRUE(
      result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightBeginningWithLineBreakIsEmpty) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "\n次の行";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_TRUE(
      result.right.prompt_context.empty());

  EXPECT_EQ(
      result.right.context_class,
      "empty");

  EXPECT_FALSE(
      result.right.allowed_for_prompt);

  EXPECT_EQ(
      result.right.reason,
      "empty_context");
}

TEST(ZenzContextAssemblerTest,
     RightTrailingHorizontalSpaceBeforeLineBreakIsEmpty) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "  \n次の行";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_TRUE(
      result.right.prompt_context.empty());

  EXPECT_EQ(
      result.right.context_class,
      "empty");

  EXPECT_FALSE(
      result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     PreservesUnicodeCharacterBoundaries) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "甲😀乙";
  input.following_text = "甲😀乙";
  input.left_max_chars = 2;
  input.right_max_chars = 2;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "😀乙");

  EXPECT_EQ(
      result.right.prompt_context,
      "甲😀");
}

TEST(ZenzContextAssemblerTest,
     ZeroLimitsProduceEmptySides) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "日本語";
  input.following_text = "日本語";
  input.left_max_chars = 0;
  input.right_max_chars = 0;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_TRUE(
      result.left.prompt_context.empty());

  EXPECT_TRUE(
      result.right.prompt_context.empty());

  EXPECT_EQ(
      result.left.context_class,
      "empty");

  EXPECT_EQ(
      result.right.context_class,
      "empty");

  EXPECT_EQ(
      result.left.reason,
      "empty_context");

  EXPECT_EQ(
      result.right.reason,
      "empty_context");
}

TEST(ZenzContextAssemblerTest,
     SanitizesLeftAndRightIndependently) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "パスワードを変更";
  input.following_text =
      "安全な文脈";
  input.left_max_chars = 128;
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_TRUE(
      result.left.prompt_context.empty());

  EXPECT_EQ(
      result.left.context_class,
      "sensitive_like");

  EXPECT_FALSE(
      result.left.allowed_for_prompt);

  EXPECT_EQ(
      result.left.reason,
      "sensitive_context_rejected");

  EXPECT_EQ(
      result.right.prompt_context,
      "安全な文脈");

  EXPECT_TRUE(
      result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     SelectionStillOccursBeforeSanitizerClassification) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "token=abcdef 日本語";
  input.left_max_chars = 3;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "日本語");

  EXPECT_EQ(
      result.left.context_class,
      "japanese_only");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);

  EXPECT_EQ(
      result.left.reason,
      "context_allowed");
}

TEST(ZenzContextAssemblerTest,
     LeftParagraphSelectionCanExcludeEarlierSensitiveText) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "password=secret\n\n現在の安全な文脈";
  input.left_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.left.prompt_context,
      "現在の安全な文脈");

  EXPECT_TRUE(
      result.left.allowed_for_prompt);

  EXPECT_EQ(
      result.left.reason,
      "context_allowed");
}

TEST(ZenzContextAssemblerTest,
     SelectedCurrentParagraphStillReceivesPrivacyProtection) {
  ZenzContextAssemblyInput input;
  input.preceding_text =
      "前の段落\n\npasswordを変更";
  input.left_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_TRUE(
      result.left.prompt_context.empty());

  EXPECT_EQ(
      result.left.context_class,
      "sensitive_like");

  EXPECT_FALSE(
      result.left.allowed_for_prompt);

  EXPECT_EQ(
      result.left.reason,
      "sensitive_context_rejected");
}

TEST(ZenzContextAssemblerTest,
     RightParagraphSelectionCanExcludeLaterSensitiveText) {
  ZenzContextAssemblyInput input;
  input.following_text =
      "安全な文脈\n\nhttps://example.com";
  input.right_max_chars = 128;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(
      result.right.prompt_context,
      "安全な文脈");

  EXPECT_TRUE(
      result.right.allowed_for_prompt);

  EXPECT_EQ(
      result.right.reason,
      "context_allowed");
}

}  // namespace
}  // namespace session
}  // namespace mozc
