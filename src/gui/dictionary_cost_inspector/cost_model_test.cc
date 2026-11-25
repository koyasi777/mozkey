// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#include "gui/dictionary_cost_inspector/cost_model.h"

#include <cstdint>
#include <string>

#include "protocol/commands.pb.h"
#include "testing/gunit.h"

namespace mozc::gui::cost_inspector {
namespace {

TEST(DictionaryCostInspectorModelTest, ValidUtf8ByteBounds) {
  EXPECT_FALSE(ValidReading("", 128));
  EXPECT_TRUE(ValidReading(std::string(300, 'a'), 128));
  EXPECT_FALSE(ValidReading(std::string(301, 'a'), 128));
  // 100 characters but 300 UTF-8 bytes.
  const std::string hiragana = "あ";
  std::string reading;
  for (int i = 0; i < 100; ++i) reading += hiragana;
  EXPECT_EQ(reading.size(), 300);
  EXPECT_TRUE(ValidReading(reading, 128));
  reading += hiragana;
  EXPECT_FALSE(ValidReading(reading, 128));
  EXPECT_FALSE(ValidReading(std::string("\xc0\xaf", 2), 128));
}

TEST(DictionaryCostInspectorModelTest, PerSourceLimit) {
  EXPECT_FALSE(ValidReading("あ", 0));
  EXPECT_TRUE(ValidReading("あ", 1));
  EXPECT_TRUE(ValidReading("あ", 256));
  EXPECT_FALSE(ValidReading("あ", 257));
}

TEST(DictionaryCostInspectorModelTest, PreservesHomographsSourceAndGrammarIds) {
  commands::DictionaryCostLookupResult result;
  auto* a = result.add_entries();
  a->set_source(commands::DictionaryCostEntry::SYSTEM);
  a->set_reading("はし");
  a->set_value("橋");
  a->set_cost(1800);
  a->set_lid(10);
  a->set_rid(20);
  a->set_attributes(1);
  auto* b = result.add_entries();
  *b = *a;
  b->set_lid(11);
  b->set_cost(1200);
  auto* c = result.add_entries();
  *c = *a;
  c->set_source(commands::DictionaryCostEntry::USER);
  c->set_cost(900);
  const auto rows = CopyRows(result);
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows[0].cost, 1800);
  EXPECT_EQ(rows[1].lid, 11);
  EXPECT_EQ(rows[2].source, commands::DictionaryCostEntry::USER);
  EXPECT_EQ(rows[2].value, "橋");
}

TEST(DictionaryCostInspectorModelTest, EmptyIsNotAnErrorAtModelLayer) {
  commands::DictionaryCostLookupResult result;
  EXPECT_TRUE(CopyRows(result).empty());
}

}  // namespace
}  // namespace mozc::gui::cost_inspector
