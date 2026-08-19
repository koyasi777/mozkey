// Copyright 2026
// Licensed under the same terms as Mozc.

#include "session/vertical_writing_key_transform.h"

#include <array>

#include "composer/key_parser.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/keymap.h"
#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

keymap::KeyMapManager MakeKeyMap(config::Config::SessionKeymap profile) {
  config::Config config;
  config.set_session_keymap(profile);
  return keymap::KeyMapManager(config);
}

commands::KeyEvent ParseKey(const char* text) {
  commands::KeyEvent key;
  EXPECT_TRUE(KeyParser::ParseKey(text, &key));
  return key;
}

TEST(VerticalWritingKeyTransformTest, HorizontalWritingIsUnchanged) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);
  commands::KeyEvent key = ParseKey("Left");

  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      false, VerticalWritingKeyState::kSuggestion, keymap, &key));
  EXPECT_EQ(key.special_key(), commands::KeyEvent::LEFT);
}

TEST(VerticalWritingKeyTransformTest,
     SuggestionLeftUsesExistingPredictAndConvertBinding) {
  for (const config::Config::SessionKeymap profile :
       {config::Config::MSIME, config::Config::KOTOERI}) {
    SCOPED_TRACE(static_cast<int>(profile));
    keymap::KeyMapManager keymap = MakeKeyMap(profile);
    commands::KeyEvent key = ParseKey("Left");

    EXPECT_TRUE(TransformVerticalWritingCandidateArrowKey(
        true, VerticalWritingKeyState::kSuggestion, keymap, &key));
    EXPECT_EQ(key.special_key(), commands::KeyEvent::DOWN);
  }
}

TEST(VerticalWritingKeyTransformTest,
     SuggestionRightDoesNotPretendCandidateIsFocused) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);
  commands::KeyEvent key = ParseKey("Right");

  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kSuggestion, keymap, &key));
  EXPECT_EQ(key.special_key(), commands::KeyEvent::RIGHT);
}

TEST(VerticalWritingKeyTransformTest, ConversionRotatesVisualArrowSemantics) {
  struct TestCase {
    const char* physical;
    commands::KeyEvent::SpecialKey logical;
  };
  constexpr std::array<TestCase, 4> kCases = {{
      {"Left", commands::KeyEvent::DOWN},
      {"Right", commands::KeyEvent::UP},
      {"Up", commands::KeyEvent::LEFT},
      {"Down", commands::KeyEvent::RIGHT},
  }};

  for (const config::Config::SessionKeymap profile :
       {config::Config::MSIME, config::Config::KOTOERI}) {
    keymap::KeyMapManager keymap = MakeKeyMap(profile);
    for (const TestCase& test : kCases) {
      SCOPED_TRACE(static_cast<int>(profile));
      SCOPED_TRACE(test.physical);
      commands::KeyEvent key = ParseKey(test.physical);

      EXPECT_TRUE(TransformVerticalWritingCandidateArrowKey(
          true, VerticalWritingKeyState::kConversion, keymap, &key));
      EXPECT_EQ(key.special_key(), test.logical);
    }
  }
}

TEST(VerticalWritingKeyTransformTest, PredictionLeftAndRightMoveCandidates) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);

  commands::KeyEvent left = ParseKey("Left");
  EXPECT_TRUE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kPrediction, keymap, &left));
  EXPECT_EQ(left.special_key(), commands::KeyEvent::DOWN);

  commands::KeyEvent right = ParseKey("Right");
  EXPECT_TRUE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kPrediction, keymap, &right));
  EXPECT_EQ(right.special_key(), commands::KeyEvent::UP);
}

TEST(VerticalWritingKeyTransformTest,
     VerticalConversionShiftUpAndDownResizeSegment) {
  for (const config::Config::SessionKeymap profile :
       {config::Config::MSIME, config::Config::KOTOERI}) {
    SCOPED_TRACE(static_cast<int>(profile));
    keymap::KeyMapManager keymap = MakeKeyMap(profile);

    commands::KeyEvent shrink = ParseKey("Shift Up");
    EXPECT_TRUE(TransformVerticalWritingCandidateArrowKey(
        true, VerticalWritingKeyState::kConversion, keymap, &shrink));
    EXPECT_EQ(shrink.special_key(), commands::KeyEvent::LEFT);
    keymap::ConversionState::Commands shrink_command =
        keymap::ConversionState::NONE;
    ASSERT_TRUE(keymap.GetCommandConversion(shrink, &shrink_command));
    EXPECT_EQ(shrink_command, keymap::ConversionState::SEGMENT_WIDTH_SHRINK);

    commands::KeyEvent expand = ParseKey("Shift Down");
    EXPECT_TRUE(TransformVerticalWritingCandidateArrowKey(
        true, VerticalWritingKeyState::kConversion, keymap, &expand));
    EXPECT_EQ(expand.special_key(), commands::KeyEvent::RIGHT);
    keymap::ConversionState::Commands expand_command =
        keymap::ConversionState::NONE;
    ASSERT_TRUE(keymap.GetCommandConversion(expand, &expand_command));
    EXPECT_EQ(expand_command, keymap::ConversionState::SEGMENT_WIDTH_EXPAND);
  }
}

TEST(VerticalWritingKeyTransformTest,
     ExistingShiftLeftAndRightRemainCompatibilityBindings) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);

  commands::KeyEvent shrink = ParseKey("Shift Left");
  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kConversion, keymap, &shrink));
  EXPECT_EQ(shrink.special_key(), commands::KeyEvent::LEFT);

  commands::KeyEvent expand = ParseKey("Shift Right");
  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kConversion, keymap, &expand));
  EXPECT_EQ(expand.special_key(), commands::KeyEvent::RIGHT);
}

TEST(VerticalWritingKeyTransformTest,
     HorizontalShiftUpAndDownKeepPageNavigation) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);

  commands::KeyEvent up = ParseKey("Shift Up");
  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      false, VerticalWritingKeyState::kConversion, keymap, &up));
  EXPECT_EQ(up.special_key(), commands::KeyEvent::UP);

  commands::KeyEvent down = ParseKey("Shift Down");
  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      false, VerticalWritingKeyState::kConversion, keymap, &down));
  EXPECT_EQ(down.special_key(), commands::KeyEvent::DOWN);
}

TEST(VerticalWritingKeyTransformTest,
     PredictionShiftUpAndDownRemainUnchanged) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);

  commands::KeyEvent up = ParseKey("Shift Up");
  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kPrediction, keymap, &up));
  EXPECT_EQ(up.special_key(), commands::KeyEvent::UP);

  commands::KeyEvent down = ParseKey("Shift Down");
  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kPrediction, keymap, &down));
  EXPECT_EQ(down.special_key(), commands::KeyEvent::DOWN);
}

TEST(VerticalWritingKeyTransformTest,
     CtrlShiftArrowIsNotReinterpreted) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);
  commands::KeyEvent key = ParseKey("Ctrl Shift Up");

  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kConversion, keymap, &key));
  EXPECT_EQ(key.special_key(), commands::KeyEvent::UP);
}

TEST(VerticalWritingKeyTransformTest, OrdinaryCompositionIsOutOfScope) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::MSIME);
  commands::KeyEvent key = ParseKey("Up");

  EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
      true, VerticalWritingKeyState::kOther, keymap, &key));
  EXPECT_EQ(key.special_key(), commands::KeyEvent::UP);
}

TEST(VerticalWritingKeyTransformTest, AtokArrowContractIsPreserved) {
  keymap::KeyMapManager keymap = MakeKeyMap(config::Config::ATOK);

  for (const VerticalWritingKeyState state :
       {VerticalWritingKeyState::kSuggestion,
        VerticalWritingKeyState::kConversion,
        VerticalWritingKeyState::kPrediction}) {
    for (const char* physical :
         {"Left", "Right", "Up", "Down", "Shift Up", "Shift Down"}) {
      SCOPED_TRACE(static_cast<int>(state));
      SCOPED_TRACE(physical);
      commands::KeyEvent key = ParseKey(physical);
      const commands::KeyEvent::SpecialKey original = key.special_key();

      EXPECT_FALSE(TransformVerticalWritingCandidateArrowKey(
          true, state, keymap, &key));
      EXPECT_EQ(key.special_key(), original);
    }
  }
}

}  // namespace
}  // namespace session
}  // namespace mozc