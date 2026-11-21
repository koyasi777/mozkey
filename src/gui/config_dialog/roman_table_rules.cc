// Copyright 2026, Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#include "gui/config_dialog/roman_table_rules.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mozc::gui::roman_table_rules {

bool IsValidRule(const Rule& rule) {
  return !rule.input.empty() && (!rule.output.empty() || !rule.pending.empty());
}

ParseResult Parse(std::istream& input, std::size_t max_rules) {
  ParseResult result;
  std::string line;
  std::size_t physical_line = 0;
  while (std::getline(input, line)) {
    ++physical_line;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line.front() == '#') continue;
    if (line.find('\0') != std::string::npos) {
      result.error = ParseError::kInvalidRule;
      result.error_line = physical_line;
      return result;
    }
    std::vector<std::string> columns;
    std::size_t start = 0;
    while (true) {
      const std::size_t end = line.find('\t', start);
      columns.push_back(line.substr(start, end - start));
      if (end == std::string::npos) break;
      start = end + 1;
    }
    if (columns.size() < 2 || columns.size() > 4) {
      result.error = ParseError::kInvalidRule;
      result.error_line = physical_line;
      return result;
    }
    Rule rule{columns[0], columns[1],
              columns.size() >= 3 ? columns[2] : "",
              columns.size() >= 4 ? columns[3] : ""};
    if (!IsValidRule(rule)) {
      result.error = ParseError::kInvalidRule;
      result.error_line = physical_line;
      return result;
    }
    if (result.rules.size() >= max_rules) {
      result.error = ParseError::kTooManyRules;
      result.error_line = physical_line;
      return result;
    }
    result.rules.push_back(std::move(rule));
  }
  if (input.bad()) {
    result.error = ParseError::kIoError;
  } else if (result.rules.empty()) {
    result.error = ParseError::kNoRules;
  }
  return result;
}

std::string Serialize(const std::vector<Rule>& rules) {
  std::string text;
  for (const Rule& rule : rules) {
    text += rule.input;
    text += '\t';
    text += rule.output;
    if (!rule.pending.empty() || !rule.attributes.empty()) {
      text += '\t';
      text += rule.pending;
    }
    if (!rule.attributes.empty()) {
      text += '\t';
      text += rule.attributes;
    }
    text += '\n';
  }
  return text;
}

bool HasConflictingInputKeys(const std::vector<Rule>& rules) {
  std::unordered_map<std::string, Rule> first;
  for (const Rule& rule : rules) {
    const auto [it, inserted] = first.try_emplace(rule.input, rule);
    if (!inserted && (it->second.output != rule.output ||
                      it->second.pending != rule.pending ||
                      it->second.attributes != rule.attributes)) {
      return true;
    }
  }
  return false;
}

}  // namespace mozc::gui::roman_table_rules
