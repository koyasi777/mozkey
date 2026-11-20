// Copyright 2026, Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#include "gui/config_dialog/roman_table_rules.h"

#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace mozc::gui::roman_table_rules {
namespace {

TEST(RomanTableRulesTest, StandardIdenticalDuplicateIsHarmless) {
  std::istringstream input("fu\tふ\nfu\tふ\n");
  const ParseResult parsed = Parse(input, 4000);
  ASSERT_TRUE(parsed.ok());
  ASSERT_EQ(parsed.rules.size(), 2);
  EXPECT_FALSE(HasConflictingInputKeys(parsed.rules));
  EXPECT_EQ(Serialize(parsed.rules), "fu\tふ\nfu\tふ\n");
}

TEST(RomanTableRulesTest, ConflictingOutputPendingOrAttributes) {
  for (const std::string& different : {
           "ka\tカ", "ka\tか\tk", "ka\tか\t\tDisplayAmbiguousResult"}) {
    std::istringstream input("ka\tか\n" + different + "\n");
    const ParseResult parsed = Parse(input, 4000);
    ASSERT_TRUE(parsed.ok());
    EXPECT_TRUE(HasConflictingInputKeys(parsed.rules)) << different;
  }
}

TEST(RomanTableRulesTest, EmptyTrailingColumnsAreEquivalent) {
  std::istringstream input("ka\tか\nka\tか\t\nka\tか\t\t\n");
  const ParseResult parsed = Parse(input, 4000);
  ASSERT_TRUE(parsed.ok());
  EXPECT_FALSE(HasConflictingInputKeys(parsed.rules));
}

TEST(RomanTableRulesTest, ParseAndSerializePreserveOrderAndColumns) {
  std::istringstream input("# comment\r\nki\tき\r\nka\tか\t\tOther\r\n"
                           "kk\tっ\tk\r\n");
  const ParseResult parsed = Parse(input, 4000);
  ASSERT_TRUE(parsed.ok());
  ASSERT_EQ(parsed.rules.size(), 3);
  EXPECT_EQ(parsed.rules[0].input, "ki");
  EXPECT_EQ(parsed.rules[1].input, "ka");
  EXPECT_EQ(parsed.rules[1].attributes, "Other");
  EXPECT_EQ(parsed.rules[2].pending, "k");
  EXPECT_EQ(Serialize(parsed.rules), "ki\tき\nka\tか\t\tOther\nkk\tっ\tk\n");
}

TEST(RomanTableRulesTest, RejectsIncompleteAndMalformedRowsWithPhysicalLine) {
  for (const std::string& invalid : {"a", "\tあ", "ka\t", "ka\t\t",
                                     "ka\tか\t\t\textra"}) {
    std::istringstream input("# comment\nki\tき\n" + invalid + "\n");
    const ParseResult parsed = Parse(input, 4000);
    EXPECT_EQ(parsed.error, ParseError::kInvalidRule) << invalid;
    EXPECT_EQ(parsed.error_line, 3) << invalid;
  }
}

TEST(RomanTableRulesTest, RejectsNulInInput) {
  std::string bytes = "ka\tか\n";
  bytes.insert(2, 1, '\0');
  std::istringstream input(bytes);
  EXPECT_EQ(Parse(input, 4000).error, ParseError::kInvalidRule);
}

TEST(RomanTableRulesTest, RejectsTooManyOrEmpty) {
  std::istringstream too_many("ka\tか\nki\tき\n");
  const ParseResult limited = Parse(too_many, 1);
  EXPECT_EQ(limited.error, ParseError::kTooManyRules);
  EXPECT_EQ(limited.error_line, 2);
  std::istringstream empty("# comment\n\n");
  EXPECT_EQ(Parse(empty, 4000).error, ParseError::kNoRules);
}

TEST(RomanTableRulesTest, IncompleteEditorRowIsNotValid) {
  EXPECT_FALSE(IsValidRule(Rule{"ka", "", "", ""}));
  EXPECT_FALSE(IsValidRule(Rule{"", "か", "", ""}));
  EXPECT_TRUE(IsValidRule(Rule{"k", "", "k", ""}));
  EXPECT_TRUE(IsValidRule(Rule{"ka", "か", "", ""}));
}

}  // namespace
}  // namespace mozc::gui::roman_table_rules
