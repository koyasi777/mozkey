// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#include "gui/dictionary_cost_inspector/cost_model.h"

#include <cstdint>
#include <vector>

#include "base/util.h"
#include "protocol/dictionary_cost_limits.h"

namespace mozc::gui::cost_inspector {

bool ValidReading(absl::string_view reading, uint32_t per_source_limit) {
  return !reading.empty() &&
         reading.size() <= commands::kDictionaryCostMaxReadingBytes &&
         Util::IsValidUtf8(reading) &&
         per_source_limit >= 1 &&
         per_source_limit <= commands::kDictionaryCostMaxEntriesPerSource;
}

std::vector<CostRow> CopyRows(const commands::DictionaryCostLookupResult& result) {
  std::vector<CostRow> rows;
  rows.reserve(result.entries_size());
  for (const commands::DictionaryCostEntry& entry : result.entries()) {
    rows.push_back({entry.source(), entry.reading(), entry.value(),
                    entry.cost(), entry.lid(), entry.rid(), entry.attributes()});
  }
  return rows;
}

}  // namespace mozc::gui::cost_inspector
