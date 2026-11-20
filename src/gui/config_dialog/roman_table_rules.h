// Copyright 2026, Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZC_GUI_CONFIG_DIALOG_ROMAN_TABLE_RULES_H_
#define MOZC_GUI_CONFIG_DIALOG_ROMAN_TABLE_RULES_H_

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

namespace mozc::gui::roman_table_rules {

// Preserve physical order: Mozc's composer can be sensitive to rule order.
struct Rule {
  std::string input;
  std::string output;
  std::string pending;
  std::string attributes;
};

enum class ParseError {
  kNone,
  kInvalidRule,
  kTooManyRules,
  kNoRules,
  kIoError,
};

struct ParseResult {
  std::vector<Rule> rules;
  ParseError error = ParseError::kNone;
  std::size_t error_line = 0;
  bool ok() const { return error == ParseError::kNone; }
};

bool IsValidRule(const Rule& rule);
ParseResult Parse(std::istream& input, std::size_t max_rules);
std::string Serialize(const std::vector<Rule>& rules);
bool HasConflictingInputKeys(const std::vector<Rule>& rules);

}  // namespace mozc::gui::roman_table_rules
#endif  // MOZC_GUI_CONFIG_DIALOG_ROMAN_TABLE_RULES_H_
