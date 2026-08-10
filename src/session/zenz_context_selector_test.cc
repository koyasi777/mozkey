#include "session/zenz_context_selector.h"

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzContextSelectorTest,
     LeftCrossesSingleLfWithinSameParagraph) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "前の行\n現在の文脈",
          128),
      "前の行\n現在の文脈");
}

TEST(ZenzContextSelectorTest,
     LeftCrossesSingleCrLfWithinSameParagraph) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "前の行\r\n現在の文脈",
          128),
      "前の行\r\n現在の文脈");
}

TEST(ZenzContextSelectorTest,
     LeftStopsAtBlankLine) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "前の段落\n\n現在の段落",
          128),
      "現在の段落");
}

TEST(ZenzContextSelectorTest,
     LeftStopsAtCrLfBlankLine) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "前の段落\r\n\r\n現在の段落",
          128),
      "現在の段落");
}

TEST(ZenzContextSelectorTest,
     LeftTreatsWhitespaceOnlyLineAsParagraphBoundary) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "前の段落\n  \t\n現在の段落",
          128),
      "現在の段落");
}

TEST(ZenzContextSelectorTest,
     LeftTreatsIdeographicSpaceOnlyLineAsParagraphBoundary) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "前の段落\n　\n現在の段落",
          128),
      "現在の段落");
}

TEST(ZenzContextSelectorTest,
     LeftSentenceTerminatorIsNotHardBoundary) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "方式Aを提案した。その方法では",
          128),
      "方式Aを提案した。その方法では");
}

TEST(ZenzContextSelectorTest,
     LeftBudgetIsAppliedAfterParagraphSelection) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "前の段落\n\n甲乙丙丁",
          2),
      "丙丁");
}

TEST(ZenzContextSelectorTest,
     LeftBudgetPreservesUnicodeCodepointBoundaries) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectLeft(
          "甲😀乙",
          2),
      "😀乙");
}

TEST(ZenzContextSelectorTest,
     LeftZeroBudgetIsEmpty) {
  const ZenzContextSelector selector;

  EXPECT_TRUE(
      selector.SelectLeft(
          "日本語",
          0).empty());
}

TEST(ZenzContextSelectorTest,
     RightStopsAfterJapaneseSentenceTerminator) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "を採用する。次の議題に移る",
          128),
      "を採用する。");
}

TEST(ZenzContextSelectorTest,
     RightStopsAfterFullWidthQuestionOrExclamation) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "でよい？次の文",
          128),
      "でよい？");

  EXPECT_EQ(
      selector.SelectRight(
          "問題ない！次の文",
          128),
      "問題ない！");
}

TEST(ZenzContextSelectorTest,
     RightStopsAfterAsciiQuestionOrExclamation) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "でよい?次の文",
          128),
      "でよい?");

  EXPECT_EQ(
      selector.SelectRight(
          "問題ない!次の文",
          128),
      "問題ない!");
}

TEST(ZenzContextSelectorTest,
     RightKeepsRepeatedTerminatorsAndClosingQuote) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "だった！？」次の文",
          128),
      "だった！？」");
}
TEST(ZenzContextSelectorTest,
     RightKeepsClosingBracketAfterSentenceTerminator) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "だった。）次の文",
          128),
      "だった。）");
}

TEST(ZenzContextSelectorTest,
     RightCrossesSingleLfInsideContinuation) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "を採用し\n継続する。次の文",
          128),
      "を採用し\n継続する。");
}

TEST(ZenzContextSelectorTest,
     RightCrossesSingleCrLfInsideContinuation) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "を採用し\r\n継続する。次の文",
          128),
      "を採用し\r\n継続する。");
}

TEST(ZenzContextSelectorTest,
     RightStopsBeforeBlankLine) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "を採用する\n\n次の段落",
          128),
      "を採用する");
}

TEST(ZenzContextSelectorTest,
     RightStopsBeforeWhitespaceOnlyBlankLine) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "を採用する\n \t\n次の段落",
          128),
      "を採用する");
}

TEST(ZenzContextSelectorTest,
     RightBeginningWithAsciiSpacesThenLfIsEmpty) {
  const ZenzContextSelector selector;

  EXPECT_TRUE(
      selector.SelectRight(
          "   \n次の行",
          128).empty());
}

TEST(ZenzContextSelectorTest,
     RightBeginningWithTabThenCrLfIsEmpty) {
  const ZenzContextSelector selector;

  EXPECT_TRUE(
      selector.SelectRight(
          "\t\r\n次の行",
          128).empty());
}

TEST(ZenzContextSelectorTest,
     RightBeginningWithIdeographicSpaceThenLfIsEmpty) {
  const ZenzContextSelector selector;

  EXPECT_TRUE(
      selector.SelectRight(
          "　\n次の行",
          128).empty());
}

TEST(ZenzContextSelectorTest,
     RightLeadingSpacesWithoutLineBreakArePreserved) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "  を採用する。次の文",
          128),
      "  を採用する。");
}

TEST(ZenzContextSelectorTest,
     RightBeginningWithLfIsEmpty) {
  const ZenzContextSelector selector;

  EXPECT_TRUE(
      selector.SelectRight(
          "\n次の行",
          128).empty());
}

TEST(ZenzContextSelectorTest,
     RightBeginningWithCrLfIsEmpty) {
  const ZenzContextSelector selector;

  EXPECT_TRUE(
      selector.SelectRight(
          "\r\n次の行",
          128).empty());
}

TEST(ZenzContextSelectorTest,
     RightBudgetIsAppliedFromLeadingEdge) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "甲乙丙丁",
          2),
      "甲乙");
}

TEST(ZenzContextSelectorTest,
     RightBudgetPreservesUnicodeCodepointBoundaries) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "甲😀乙",
          2),
      "甲😀");
}

TEST(ZenzContextSelectorTest,
     RightWithoutSentenceBoundaryUsesAvailableParagraph) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "を採用して継続する",
          128),
      "を採用して継続する");
}

TEST(ZenzContextSelectorTest,
     RightDoesNotIncludeIndependentNextSentence) {
  const ZenzContextSelector selector;

  EXPECT_EQ(
      selector.SelectRight(
          "である。次の文も存在する。",
          128),
      "である。");
}

TEST(ZenzContextSelectorTest,
     RightZeroBudgetIsEmpty) {
  const ZenzContextSelector selector;

  EXPECT_TRUE(
      selector.SelectRight(
          "日本語",
          0).empty());
}

}  // namespace
}  // namespace session
}  // namespace mozc