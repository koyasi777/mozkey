#include "renderer/conversion_status_indicator.h"

#include "testing/gunit.h"

namespace mozc::renderer {
namespace {
commands::Output PreeditOutput() {
  commands::Output out;
  out.mutable_preedit()->add_segment()->set_value("abc");
  return out;
}
TEST(ConversionStatusIndicatorTest, DefaultSettingsAndExplicitOverrides) {
  config::Config config;
  EXPECT_FALSE(config.has_show_conversion_status_indicator());
  EXPECT_TRUE(config.show_conversion_status_indicator());
  const auto& style = config.conversion_status_indicator_style();
  EXPECT_EQ(style.zenz_pending_color(), 0x847438u);
  EXPECT_EQ(style.zenz_color(), 0x338d85u);

  // A stored opt-out must not be silently changed by the new default.
  config.set_show_conversion_status_indicator(false);
  EXPECT_TRUE(config.has_show_conversion_status_indicator());
  EXPECT_FALSE(config.show_conversion_status_indicator());

  // Previously customized colors are preserved after serialization.
  auto* custom = config.mutable_conversion_status_indicator_style();
  custom->set_zenz_pending_color(0x112233u);
  custom->set_zenz_color(0x445566u);
  config::Config restored;
  ASSERT_TRUE(restored.ParseFromString(config.SerializeAsString()));
  EXPECT_FALSE(restored.show_conversion_status_indicator());
  EXPECT_EQ(restored.conversion_status_indicator_style().zenz_pending_color(),
            0x112233u);
  EXPECT_EQ(restored.conversion_status_indicator_style().zenz_color(),
            0x445566u);
}
TEST(ConversionStatusIndicatorTest, PassiveLiveSuggestionKeepsStatus) {
  auto out = PreeditOutput();
  out.set_live_conversion(true);
  auto* candidates = out.mutable_candidate_window();
  candidates->set_category(commands::SUGGESTION);
  candidates->add_candidate()->set_value("candidate");
  EXPECT_FALSE(ShouldHideConversionStatusForCandidate(out, false));
  EXPECT_FALSE(ShouldHideConversionStatusForCandidate(out, true));
  // Moving the selection focus into the list must suppress the status.
  candidates->set_focused_index(0);
  EXPECT_TRUE(ShouldHideConversionStatusForCandidate(out, true));
}
TEST(ConversionStatusIndicatorTest, NormalSuggestionsAndConversionHideStatus) {
  auto out = PreeditOutput();
  auto* candidates = out.mutable_candidate_window();
  candidates->set_category(commands::SUGGESTION);
  candidates->add_candidate()->set_value("candidate");
  out.set_pre_live_conversion_reading(true);
  EXPECT_EQ(ResolveConversionDisplayStatus(out), ConversionDisplayStatus::kReading);
  // An unfocused normal suggestion must not erase the reading indicator.
  EXPECT_FALSE(ShouldHideConversionStatusForCandidate(out, true));
  candidates->set_focused_index(0);
  EXPECT_TRUE(ShouldHideConversionStatusForCandidate(out, true));
  candidates->clear_focused_index();
  out.set_live_conversion(true);
  candidates->set_category(commands::CONVERSION);
  EXPECT_TRUE(ShouldHideConversionStatusForCandidate(out, true));
  candidates->set_category(commands::PREDICTION);
  EXPECT_TRUE(ShouldHideConversionStatusForCandidate(out, true));
  candidates->set_category(commands::SUGGESTION);
  candidates->clear_candidate();
  EXPECT_TRUE(ShouldHideConversionStatusForCandidate(out, true));
}
TEST(ConversionStatusIndicatorTest, HiddenWithoutPreedit) {
  commands::Output out;
  EXPECT_EQ(ResolveConversionDisplayStatus(out), ConversionDisplayStatus::kHidden);
}
TEST(ConversionStatusIndicatorTest, ReadingIsNotMozcConversion) {
  auto out = PreeditOutput();
  out.set_pre_live_conversion_reading(true);
  EXPECT_EQ(ResolveConversionDisplayStatus(out), ConversionDisplayStatus::kReading);
  out.set_live_conversion_pending(true);
  EXPECT_EQ(ResolveConversionDisplayStatus(out), ConversionDisplayStatus::kReading);
}
TEST(ConversionStatusIndicatorTest, LiveMozcConversion) {
  auto out = PreeditOutput();
  out.set_live_conversion(true);
  EXPECT_EQ(ResolveConversionDisplayStatus(out), ConversionDisplayStatus::kMozc);
}
TEST(ConversionStatusIndicatorTest, PendingHasPriorityOverApplied) {
  auto out = PreeditOutput();
  out.set_live_conversion(true);
  out.set_zenz_live_correction_applied(true);
  out.set_zenz_live_correction_pending(true);
  EXPECT_EQ(ResolveConversionDisplayStatus(out), ConversionDisplayStatus::kZenzPending);
  out.set_zenz_live_correction_pending(false);
  EXPECT_EQ(ResolveConversionDisplayStatus(out), ConversionDisplayStatus::kZenz);
}
TEST(ConversionStatusIndicatorTest, AutoAboveCompositionWithoutRuby) {
  config::Config::ConversionStatusIndicatorStyle style;
  Rect result;
  EXPECT_TRUE(PlaceConversionStatusIndicator(
      Rect(100, 100, 20, 22), nullptr, nullptr,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), 100);
  EXPECT_EQ(result.Bottom(), 95);
}
TEST(ConversionStatusIndicatorTest, AutoUsesRubySideWhenPresent) {
  config::Config::ConversionStatusIndicatorStyle style;
  Rect ruby(100, 60, 75, 24), result;
  EXPECT_TRUE(PlaceConversionStatusIndicator(
      Rect(100, 100, 20, 22), &ruby, nullptr,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), 180);
  EXPECT_EQ(result.Top(), 60);
}
TEST(ConversionStatusIndicatorTest, RubySideAvoidsVisibleCandidate) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  const Rect ruby(100, 60, 75, 24);
  const Rect visible_candidate(180, 60, 58, 24);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(100, 100, 20, 22), &ruby, &visible_candidate,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
  // The right side is occupied, so use the left without overlapping ruby.
  EXPECT_EQ(result.Left(), 37);
  EXPECT_EQ(result.Top(), 60);
}
TEST(ConversionStatusIndicatorTest, ExplicitAboveKeepsRequestedSide) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_PREEDIT_ABOVE);
  Rect obstacle(100, 70, 58, 24), result;
  EXPECT_TRUE(PlaceConversionStatusIndicator(
      Rect(100, 100, 20, 22), nullptr, &obstacle,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Top(), 71);
  EXPECT_FALSE(result.Left() == 100);
}
TEST(ConversionStatusIndicatorTest, ExplicitBelowIsNotMovedAbove) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_PREEDIT_BELOW);
  Rect ruby(100, 65, 100, 22), result;
  EXPECT_TRUE(PlaceConversionStatusIndicator(
      Rect(100, 100, 20, 22), &ruby, nullptr,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Top(), 127);
  EXPECT_EQ(result.Left(), 100);
}
TEST(ConversionStatusIndicatorTest, BelowAvoidsCandidateOnSameRow) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_PREEDIT_BELOW);
  Rect obstacle(100, 126, 58, 35), result;
  EXPECT_TRUE(PlaceConversionStatusIndicator(
      Rect(100, 100, 20, 22), nullptr, &obstacle,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Top(), 127);
  EXPECT_EQ(result.Left(), 163);
}
TEST(ConversionStatusIndicatorTest, UnavailableBelowDoesNotFallBackAbove) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_PREEDIT_BELOW);
  Rect result;
  EXPECT_FALSE(PlaceConversionStatusIndicator(
      Rect(100, 370, 20, 22), nullptr, nullptr,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
}
TEST(ConversionStatusIndicatorTest, RubyPlacementWorksWithoutRubyWindow) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  Rect result;
  EXPECT_TRUE(PlaceConversionStatusIndicator(
      Rect(100, 100, 20, 22), nullptr, nullptr,
      Rect(0, 0, 600, 400), Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), 125);
}
TEST(ConversionStatusIndicatorTest, VerticalRubySideMeansBelowRuby) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  const Rect preedit(200, 100, 24, 160), ruby(230, 95, 22, 110);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(preedit, &ruby, nullptr,
      Rect(0, 0, 600, 500), Size(24, 95), true, style, &result));
  EXPECT_EQ(result.Left(), 229);  // (22 - 24) / 2 from ruby left
  EXPECT_EQ(result.Top(), 210);
}
TEST(ConversionStatusIndicatorTest, VerticalRubyAboveMeansOutsideColumn) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_ABOVE);
  const Rect preedit(200, 100, 24, 160), ruby(230, 95, 22, 110);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(preedit, &ruby, nullptr,
      Rect(0, 0, 600, 500), Size(24, 95), true, style, &result));
  EXPECT_EQ(result.Left(), 257);
  EXPECT_EQ(result.Top(), 95);
}
TEST(ConversionStatusIndicatorTest, VerticalRubyBelowDoesNotFallBackToSide) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  const Rect preedit(200, 20, 24, 240), ruby(230, 20, 22, 240);
  Rect result;
  // At the monitor edge there is insufficient room below the ruby, and
  // above it overlaps or escapes: do not put an explicit "below" at its side.
  EXPECT_FALSE(PlaceConversionStatusIndicator(preedit, &ruby, nullptr,
      Rect(0, 0, 400, 290), Size(24, 95), true, style, &result));
}
TEST(ConversionStatusIndicatorTest, VerticalAutoPrefersBelowRuby) {
  config::Config::ConversionStatusIndicatorStyle style;
  Rect result;
  const Rect ruby(230, 95, 22, 110);
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(200, 100, 24, 160), &ruby, nullptr,
      Rect(0, 0, 600, 500), Size(24, 95), true, style, &result));
  EXPECT_EQ(result.Top(), 210);
}

// Ruby-side placement centers across the ruby thickness; the other
// placements deliberately keep their original leading-edge anchors.
TEST(ConversionStatusIndicatorTest, HorizontalRubySideCentersVertically) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  const Rect ruby(150, 60, 110, 52);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(150, 130, 25, 24), &ruby, nullptr, Rect(0, 0, 600, 400),
      Size(74, 20), false, style, &result));
  EXPECT_EQ(result.Left(), 265);
  EXPECT_EQ(result.Top(), 76);
  EXPECT_EQ(2 * result.Top() + result.Height(),
            2 * ruby.Top() + ruby.Height());
}
TEST(ConversionStatusIndicatorTest, HorizontalRubySideFallbackKeepsCenter) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  const Rect ruby(230, 60, 40, 52);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(50, 200, 20, 20), &ruby, nullptr, Rect(0, 0, 300, 400),
      Size(74, 20), false, style, &result));
  EXPECT_EQ(result.Left(), 151);  // Right does not fit; use left.
  EXPECT_EQ(result.Top(), 76);
}
TEST(ConversionStatusIndicatorTest, VerticalRubyBelowCentersHorizontally) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  const Rect ruby(270, 50, 54, 125);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(225, 70, 20, 145), &ruby, nullptr, Rect(0, 0, 600, 500),
      Size(22, 90), true, style, &result));
  EXPECT_EQ(result.Left(), 286);
  EXPECT_EQ(result.Top(), 180);
  EXPECT_EQ(2 * result.Left() + result.Width(),
            2 * ruby.Left() + ruby.Width());
}
TEST(ConversionStatusIndicatorTest, HorizontalRubyAboveKeepsLeftEdge) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_ABOVE);
  const Rect ruby(150, 70, 110, 30);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(150, 130, 25, 24), &ruby, nullptr, Rect(0, 0, 600, 400),
      Size(70, 24), false, style, &result));
  EXPECT_EQ(result.Left(), ruby.Left());
  EXPECT_EQ(result.Bottom(), 65);
}
TEST(ConversionStatusIndicatorTest, VerticalRubyAboveKeepsTopEdge) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_ABOVE);
  const Rect ruby(270, 50, 28, 125);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(225, 70, 20, 145), &ruby, nullptr, Rect(0, 0, 600, 500),
      Size(22, 91), true, style, &result));
  EXPECT_EQ(result.Left(), 303);
  EXPECT_EQ(result.Top(), ruby.Top());
}
TEST(ConversionStatusIndicatorTest, PreeditBasedPlacementKeepsLeftEdge) {
  config::Config::ConversionStatusIndicatorStyle style;
  const Rect preedit(100, 100, 20, 22);
  Rect result;
  style.set_placement(config::Config::CONVERSION_STATUS_PREEDIT_ABOVE);
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      preedit, nullptr, nullptr, Rect(0, 0, 600, 400),
      Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), preedit.Left());
  EXPECT_EQ(result.Bottom(), 95);
  style.set_placement(config::Config::CONVERSION_STATUS_PREEDIT_BELOW);
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      preedit, nullptr, nullptr, Rect(0, 0, 600, 400),
      Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), preedit.Left());
  EXPECT_EQ(result.Top(), 127);
}
TEST(ConversionStatusIndicatorTest, OffsetsApplyAfterRubySideCentering) {
  config::Config::ConversionStatusIndicatorStyle style;
  style.set_placement(config::Config::CONVERSION_STATUS_RUBY_SIDE);
  style.set_offset_x(7);
  style.set_offset_y(11);
  const Rect ruby(150, 60, 110, 52);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(150, 130, 25, 24), &ruby, nullptr, Rect(0, 0, 600, 400),
      Size(74, 20), false, style, &result));
  EXPECT_EQ(result.Left(), 272);
  EXPECT_EQ(result.Top(), 87);
  style.set_offset_x(-4);
  style.set_offset_y(9);
  const Rect vertical_ruby(270, 50, 54, 125);
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(225, 70, 20, 145), &vertical_ruby, nullptr,
      Rect(0, 0, 600, 500), Size(22, 90), true, style, &result));
  EXPECT_EQ(result.Left(), 282);
  EXPECT_EQ(result.Top(), 189);
}
TEST(ConversionStatusIndicatorTest, AutoTracksRubySizeButPreservesFallback) {
  config::Config::ConversionStatusIndicatorStyle style;
  const Rect preedit(150, 180, 20, 20);
  const Rect ruby_small(150, 75, 80, 22);
  const Rect ruby_large(150, 75, 112, 46);
  Rect result;
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      preedit, &ruby_small, nullptr, Rect(0, 0, 600, 400),
      Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), 235);
  EXPECT_EQ(result.Top(), 74);
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      preedit, &ruby_large, nullptr, Rect(0, 0, 600, 400),
      Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), 267);
  EXPECT_EQ(result.Top(), 86);
  // If both ruby-side directions are unavailable, Auto must still use
  // the original left-aligned ruby-above fallback (not a centered one).
  const Rect wide_ruby(0, 80, 190, 22);
  ASSERT_TRUE(PlaceConversionStatusIndicator(
      Rect(10, 120, 20, 20), &wide_ruby, nullptr,
      Rect(0, 0, 200, 300), Size(58, 24), false, style, &result));
  EXPECT_EQ(result.Left(), wide_ruby.Left());
  EXPECT_EQ(result.Top(), 51);
}

TEST(ConversionStatusIndicatorTest, NeverEscapesWorkArea) {
  config::Config::ConversionStatusIndicatorStyle style;
  Rect result;
  EXPECT_FALSE(PlaceConversionStatusIndicator(
      Rect(0, 0, 20, 20), nullptr, nullptr,
      Rect(0, 0, 40, 40), Size(58, 24), false, style, &result));
}
}  // namespace
}  // namespace mozc::renderer
