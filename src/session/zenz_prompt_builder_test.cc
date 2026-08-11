// Copyright 2010-2026, Google Inc.
// All rights reserved.

#include "session/zenz_prompt_builder.h"

#include <string>

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

constexpr char kZenzReadingBegin[] = "\xEE\xB8\x80";       // U+EE00
constexpr char kZenzOutputBegin[] = "\xEE\xB8\x81";        // U+EE01
constexpr char kZenzContextBegin[] = "\xEE\xB8\x82";       // U+EE02
constexpr char kZenzProfileBegin[] = "\xEE\xB8\x83";       // U+EE03
constexpr char kZenzTopicBegin[] = "\xEE\xB8\x84";         // U+EE04
constexpr char kZenzStyleBegin[] = "\xEE\xB8\x85";         // U+EE05
constexpr char kZenzSettingsBegin[] = "\xEE\xB8\x86";      // U+EE06
constexpr char kZenzRightContextBegin[] = "\xEE\xB8\x87";  // U+EE07

TEST(ZenzPromptBuilderCharacterizationTest,
     PreservesCurrentConditionFieldOrdering) {
  ZenzPromptOptions options;
  options.left_context = "left";
  options.right_context = "right";
  options.profile = "profile";
  options.topic = "topic";
  options.style = "style";
  options.settings = "settings";

  const ZenzPromptBuilder builder;

  EXPECT_EQ(
      std::string(kZenzContextBegin) + "left" +
          kZenzRightContextBegin + "right" +
          kZenzProfileBegin + "profile" +
          kZenzTopicBegin + "topic" +
          kZenzStyleBegin + "style" +
          kZenzSettingsBegin + "settings" +
          kZenzReadingBegin + "reading" +
          kZenzOutputBegin,
      builder.Build("reading", options));
}

TEST(ZenzPromptBuilderCharacterizationTest,
     CurrentCrLfNormalizationProducesTwoSpaces) {
  ZenzPromptOptions options;
  options.left_context = "left\r\nright";

  const ZenzPromptBuilder builder;

  EXPECT_EQ(
      std::string(kZenzContextBegin) + "left  right" +
          kZenzReadingBegin + "reading" +
          kZenzOutputBegin,
      builder.Build("reading", options));
}

TEST(ZenzPromptBuilderCharacterizationTest,
     RemovesInjectedZenzControlCodepointsFromConditions) {
  ZenzPromptOptions options;
  options.left_context =
      std::string("a") + kZenzReadingBegin + "b";
  options.right_context =
      std::string("c") + kZenzOutputBegin + "d";
  options.profile =
      std::string("e") + kZenzRightContextBegin + "f";

  const ZenzPromptBuilder builder;

  EXPECT_EQ(
      std::string(kZenzContextBegin) + "ab" +
          kZenzRightContextBegin + "cd" +
          kZenzProfileBegin + "ef" +
          kZenzReadingBegin + "reading" +
          kZenzOutputBegin,
      builder.Build("reading", options));
}

TEST(ZenzPromptBuilderCharacterizationTest,
     CurrentUnsafeControlHandlingPreservesWhitespaceAsSpaces) {
  ZenzPromptOptions options;
  options.left_context =
      std::string("a\tb\nc\rd") +
      "\x01" +
      "e" +
      "\x7F" +
      "f";

  const ZenzPromptBuilder builder;

  EXPECT_EQ(
      std::string(kZenzContextBegin) + "a b c def" +
          kZenzReadingBegin + "reading" +
          kZenzOutputBegin,
      builder.Build("reading", options));
}

TEST(ZenzPromptBuilderCharacterizationTest,
     ConditionHintsAreLimitedToSixtyFourCharacters) {
  ZenzPromptOptions options;
  options.profile = std::string(65, 'p');
  options.topic = std::string(64, 't');

  const ZenzPromptBuilder builder;

  EXPECT_EQ(
      std::string(kZenzContextBegin) +
          kZenzProfileBegin + std::string(64, 'p') +
          kZenzTopicBegin + std::string(64, 't') +
          kZenzReadingBegin + "reading" +
          kZenzOutputBegin,
      builder.Build("reading", options));
}

TEST(ZenzPromptBuilderCharacterizationTest,
     LeftAndRightContextAreNotLengthLimitedByPromptBuilder) {
  ZenzPromptOptions options;
  options.left_context = std::string(80, 'l');
  options.right_context = std::string(80, 'r');

  const ZenzPromptBuilder builder;

  EXPECT_EQ(
      std::string(kZenzContextBegin) + std::string(80, 'l') +
          kZenzRightContextBegin + std::string(80, 'r') +
          kZenzReadingBegin + "reading" +
          kZenzOutputBegin,
      builder.Build("reading", options));
}

TEST(ZenzPromptBuilderCharacterizationTest,
     EmptyRightContextOmitsRightContextControlToken) {
  ZenzPromptOptions options;
  options.left_context = "left";
  options.right_context = "";

  const ZenzPromptBuilder builder;

  EXPECT_EQ(
      std::string(kZenzContextBegin) + "left" +
          kZenzReadingBegin + "reading" +
          kZenzOutputBegin,
      builder.Build("reading", options));
}

}  // namespace
}  // namespace session
}  // namespace mozc
