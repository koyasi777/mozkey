// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause

#include "unix/ibus/preedit_handler.h"

#include <cstdint>

#include "protocol/commands.pb.h"
#include "testing/gunit.h"

namespace mozc::ibus {
namespace {

commands::Output MakePreedit(uint32_t cursor, uint32_t highlighted) {
  commands::Output output;
  commands::Preedit* preedit = output.mutable_preedit();
  preedit->set_cursor(cursor);
  preedit->set_highlighted_position(highlighted);
  return output;
}

TEST(PreeditCursorTest, NoPreeditReturnsZero) {
  const commands::Output output;
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 0);
}

TEST(PreeditCursorTest, PlainCompositionUsesEditingCursor) {
  commands::Output output = MakePreedit(4, 0);
  output.mutable_preedit()->clear_highlighted_position();
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 4);
}

TEST(PreeditCursorTest, ConventionalConversionPreservesCandidateAnchor) {
  const commands::Output output = MakePreedit(7, 2);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 2);
}

TEST(PreeditCursorTest, LiveConversionUsesEditingCursor) {
  commands::Output output = MakePreedit(7, 0);
  output.set_live_conversion(true);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 7);
}

TEST(PreeditCursorTest, PendingLiveConversionUsesEditingCursor) {
  commands::Output output = MakePreedit(5, 0);
  output.set_live_conversion_pending(true);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 5);
}

TEST(PreeditCursorTest, LiveConversionClauseBoundaryDoesNotMoveCaret) {
  commands::Output output = MakePreedit(9, 3);
  output.set_live_conversion(true);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 9);
  output.mutable_preedit()->set_highlighted_position(0);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 9);
}

TEST(PreeditCursorTest, ExplicitEditingCursorAtStartIsRespected) {
  commands::Output output = MakePreedit(0, 4);
  output.set_live_conversion(true);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 0);
}

TEST(PreeditCursorTest, ReadingFlagAloneIsNotLiveConversion) {
  commands::Output output = MakePreedit(8, 1);
  output.set_pre_live_conversion_reading(true);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 1);
}

TEST(PreeditCursorTest, LiveConversionOverridesHighlightWithCandidates) {
  commands::Output output = MakePreedit(6, 0);
  output.mutable_candidate_window();
  output.set_live_conversion(true);
  EXPECT_EQ(internal::GetPreeditCursorPosition(output), 6);
}

}  // namespace
}  // namespace mozc::ibus
