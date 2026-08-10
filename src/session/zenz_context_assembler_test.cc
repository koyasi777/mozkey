#include "session/zenz_context_assembler.h"

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzContextAssemblerTest,
     PreservesCurrentDirectionalSelectionSemantics) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "甲乙丙丁";
  input.following_text = "甲乙丙丁";
  input.left_max_chars = 2;
  input.right_max_chars = 2;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(result.left.prompt_context, "丙丁");
  EXPECT_EQ(result.right.prompt_context, "甲乙");

  EXPECT_EQ(result.left.context_class, "japanese_only");
  EXPECT_EQ(result.right.context_class, "japanese_only");

  EXPECT_TRUE(result.left.allowed_for_prompt);
  EXPECT_TRUE(result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     LeftContextCurrentlyCrossesNewline) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "前行\n現在";
  input.left_max_chars = 24;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(result.left.prompt_context, "前行\n現在");
  EXPECT_EQ(result.left.context_class, "japanese_only");
  EXPECT_TRUE(result.left.allowed_for_prompt);
  EXPECT_EQ(result.left.reason, "context_allowed");
}

TEST(ZenzContextAssemblerTest,
     LeftContextUsesTrailingUnicodeCharacters) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "前行\n現在";
  input.left_max_chars = 3;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(result.left.prompt_context, "\n現在");
  EXPECT_TRUE(result.left.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightContextCurrentlyStopsAtFirstLf) {
  ZenzContextAssemblyInput input;
  input.following_text = "同一行\n次行";
  input.right_max_chars = 24;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(result.right.prompt_context, "同一行");
  EXPECT_TRUE(result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightContextCurrentlyStopsAtFirstCr) {
  ZenzContextAssemblyInput input;
  input.following_text = "甲乙\r丙丁";
  input.right_max_chars = 24;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(result.right.prompt_context, "甲乙");
  EXPECT_TRUE(result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightContextCurrentlyStopsAtCrLf) {
  ZenzContextAssemblyInput input;
  input.following_text = "甲乙\r\n丙丁";
  input.right_max_chars = 24;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(result.right.prompt_context, "甲乙");
  EXPECT_TRUE(result.right.allowed_for_prompt);
}

TEST(ZenzContextAssemblerTest,
     RightContextBeginningWithLineBreakIsEmpty) {
  ZenzContextAssemblyInput input;
  input.following_text = "\n甲乙";
  input.right_max_chars = 24;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_TRUE(result.right.prompt_context.empty());
  EXPECT_EQ(result.right.context_class, "empty");
  EXPECT_FALSE(result.right.allowed_for_prompt);
  EXPECT_EQ(result.right.reason, "empty_context");
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

  EXPECT_EQ(result.left.prompt_context, "😀乙");
  EXPECT_EQ(result.right.prompt_context, "甲😀");
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

  EXPECT_TRUE(result.left.prompt_context.empty());
  EXPECT_TRUE(result.right.prompt_context.empty());

  EXPECT_EQ(result.left.context_class, "empty");
  EXPECT_EQ(result.right.context_class, "empty");

  EXPECT_EQ(result.left.reason, "empty_context");
  EXPECT_EQ(result.right.reason, "empty_context");
}

TEST(ZenzContextAssemblerTest,
     SanitizesLeftAndRightIndependently) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "パスワードを変更";
  input.following_text = "安全な文脈";
  input.left_max_chars = 24;
  input.right_max_chars = 24;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_TRUE(result.left.prompt_context.empty());
  EXPECT_EQ(result.left.context_class, "sensitive_like");
  EXPECT_FALSE(result.left.allowed_for_prompt);
  EXPECT_EQ(result.left.reason, "sensitive_context_rejected");

  EXPECT_EQ(result.right.prompt_context, "安全な文脈");
  EXPECT_EQ(result.right.context_class, "japanese_only");
  EXPECT_TRUE(result.right.allowed_for_prompt);
  EXPECT_EQ(result.right.reason, "context_allowed");
}

TEST(ZenzContextAssemblerTest,
     SelectionOccursBeforeCurrentSanitizerClassification) {
  ZenzContextAssemblyInput input;
  input.preceding_text = "token=abcdef 日本語";
  input.left_max_chars = 3;

  const ZenzContextAssemblyResult result =
      ZenzContextAssembler().Assemble(input);

  EXPECT_EQ(result.left.prompt_context, "日本語");
  EXPECT_EQ(result.left.context_class, "japanese_only");
  EXPECT_TRUE(result.left.allowed_for_prompt);
  EXPECT_EQ(result.left.reason, "context_allowed");
}

}  // namespace
}  // namespace session
}  // namespace mozc