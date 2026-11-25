// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZC_GUI_DICTIONARY_COST_INSPECTOR_COST_MODEL_H_
#define MOZC_GUI_DICTIONARY_COST_INSPECTOR_COST_MODEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "protocol/commands.pb.h"

namespace mozc::gui::cost_inspector {

struct CostRow {
  commands::DictionaryCostEntry::Source source;
  std::string reading;
  std::string value;
  int32_t cost;
  uint32_t lid;
  uint32_t rid;
  uint32_t attributes;
};

// No normalization: exact-byte lookup is a deliberate part of this API.
bool ValidReading(absl::string_view reading, uint32_t per_source_limit);

// Preserve source order and distinct entries, even for equal readings/values.
std::vector<CostRow> CopyRows(const commands::DictionaryCostLookupResult& result);

}  // namespace mozc::gui::cost_inspector
#endif  // MOZC_GUI_DICTIONARY_COST_INSPECTOR_COST_MODEL_H_
