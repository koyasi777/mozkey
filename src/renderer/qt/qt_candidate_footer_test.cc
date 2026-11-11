// Copyright 2026 Mozkey contributors.
#include "renderer/qt/qt_candidate_footer.h"

#include "gtest/gtest.h"
#include "protocol/candidate_window.pb.h"

namespace mozc::renderer {
namespace {

TEST(QtCandidateFooterTest, NoFooterHasNoRow) {
  commands::CandidateWindow cw;
  EXPECT_FALSE(GetQtCandidateFooter(cw).visible);
}

TEST(QtCandidateFooterTest, PassiveSuggestionHintDoesNotReserveFooterRow) {
  commands::CandidateWindow cw;
  cw.set_category(commands::SUGGESTION);
  cw.mutable_footer()->set_label("Tabキーで選択");
  EXPECT_FALSE(GetQtCandidateFooter(cw).visible);
  cw.mutable_footer()->set_sub_label("Tabキーで選択");
  EXPECT_FALSE(GetQtCandidateFooter(cw).visible);
}

TEST(QtCandidateFooterTest, ActionableSuggestionHintRemainsVisible) {
  commands::CandidateWindow cw;
  cw.set_category(commands::SUGGESTION);
  cw.mutable_footer()->set_label("Ctrl+Delで履歴削除");
  const auto footer = GetQtCandidateFooter(cw);
  EXPECT_TRUE(footer.visible);
  EXPECT_EQ(footer.label, "Ctrl+Delで履歴削除");
  EXPECT_FALSE(footer.is_sub_label);
}

TEST(QtCandidateFooterTest, SecondaryLabelRemainsVisible) {
  commands::CandidateWindow cw;
  cw.mutable_footer()->set_sub_label("補助情報");
  const auto footer = GetQtCandidateFooter(cw);
  EXPECT_TRUE(footer.visible);
  EXPECT_TRUE(footer.is_sub_label);
}

TEST(QtCandidateFooterTest, OrdinaryConversionPageNumberIsVisible) {
  commands::CandidateWindow cw;
  cw.set_category(commands::CONVERSION);
  cw.set_size(101);
  cw.set_focused_index(0);
  cw.mutable_footer()->set_index_visible(true);
  const auto footer = GetQtCandidateFooter(cw);
  EXPECT_TRUE(footer.visible);
  EXPECT_EQ(footer.index, "1/101");
}

TEST(QtCandidateFooterTest, NoFakeIndexBeforeSuggestionFocus) {
  commands::CandidateWindow cw;
  cw.set_category(commands::SUGGESTION);
  cw.set_size(101);
  cw.mutable_footer()->set_index_visible(true);
  EXPECT_FALSE(GetQtCandidateFooter(cw).visible);
}

TEST(QtCandidateFooterTest, IndexZeroOutOfBoundsDoesNotRender) {
  commands::CandidateWindow cw;
  cw.set_size(3);
  cw.set_focused_index(4);
  cw.mutable_footer()->set_index_visible(true);
  EXPECT_FALSE(GetQtCandidateFooter(cw).visible);
}

TEST(QtCandidateFooterTest, PassiveHintDoesNotHideValidIndex) {
  commands::CandidateWindow cw;
  cw.set_size(9);
  cw.set_focused_index(3);
  cw.mutable_footer()->set_label("Tabキーで選択");
  cw.mutable_footer()->set_index_visible(true);
  const auto footer = GetQtCandidateFooter(cw);
  EXPECT_TRUE(footer.visible);
  EXPECT_TRUE(footer.label.empty());
  EXPECT_EQ(footer.index, "4/9");
}

}  // namespace
}  // namespace mozc::renderer
