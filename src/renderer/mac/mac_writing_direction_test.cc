// Copyright 2026, Mozkey authors.
// All rights reserved.

#include "renderer/mac/mac_writing_direction.h"

#include "protocol/candidate_window.pb.h"
#include "protocol/renderer_command.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace mac {
namespace {

void SetPreeditRectangle(commands::RendererCommand* command, int left, int top,
                         int right, int bottom) {
  auto* rect = command->mutable_preedit_rectangle();
  rect->set_left(left);
  rect->set_top(top);
  rect->set_right(right);
  rect->set_bottom(bottom);
}

TEST(MacWritingDirectionTest, ExplicitVerticalOverridesHorizontalGeometry) {
  commands::RendererCommand command;
  SetPreeditRectangle(&command, 10, 10, 11, 30);
  command.mutable_application_info()
      ->mutable_composition_target()
      ->set_vertical_writing(true);

  EXPECT_EQ(ResolveWritingDirection(command), WritingDirection::kVertical);
}

TEST(MacWritingDirectionTest, ExplicitHorizontalOverridesVerticalGeometry) {
  commands::RendererCommand command;
  SetPreeditRectangle(&command, 10, 10, 30, 11);
  command.mutable_application_info()
      ->mutable_composition_target()
      ->set_vertical_writing(false);

  EXPECT_EQ(ResolveWritingDirection(command), WritingDirection::kHorizontal);
}

TEST(MacWritingDirectionTest, FallsBackToHistoricalVerticalGeometry) {
  commands::RendererCommand command;
  SetPreeditRectangle(&command, 10, 10, 30, 11);

  EXPECT_EQ(ResolveWritingDirection(command), WritingDirection::kVertical);
}

TEST(MacWritingDirectionTest, FallsBackToHistoricalHorizontalGeometry) {
  commands::RendererCommand command;
  SetPreeditRectangle(&command, 10, 10, 11, 30);

  EXPECT_EQ(ResolveWritingDirection(command), WritingDirection::kHorizontal);
}

TEST(MacWritingDirectionTest, DefaultsToHorizontalWithoutDirectionOrGeometry) {
  commands::RendererCommand command;

  EXPECT_EQ(ResolveWritingDirection(command), WritingDirection::kHorizontal);
}

TEST(MacWritingDirectionTest, CandidateWindowDirectionIsNotWritingDirection) {
  commands::RendererCommand command;
  auto* candidate_window =
      command.mutable_output()->mutable_candidate_window();
  candidate_window->set_direction(commands::CandidateWindow::VERTICAL);

  EXPECT_EQ(ResolveWritingDirection(command), WritingDirection::kHorizontal);
}

TEST(MacWritingDirectionTest,
     MissingExplicitValueStillUsesCompatibilityGeometry) {
  commands::RendererCommand command;
  SetPreeditRectangle(&command, 10, 10, 30, 11);
  command.mutable_application_info()->mutable_composition_target();

  EXPECT_EQ(ResolveWritingDirection(command), WritingDirection::kVertical);
}

}  // namespace
}  // namespace mac
}  // namespace renderer
}  // namespace mozc
