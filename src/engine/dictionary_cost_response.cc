// Copyright 2026, Mozkey contributors.
#include "engine/dictionary_cost_response.h"

#include <cstddef>
#include <vector>

#include "dictionary/dictionary_interface.h"
#include "protocol/commands.pb.h"
#include "protocol/dictionary_cost_limits.h"

namespace mozc::engine::internal {
namespace {

using Source = dictionary::DictionaryCostEntry::Source;

void CopyEntry(const dictionary::DictionaryCostEntry& source,
               commands::DictionaryCostEntry* destination) {
  destination->set_source(source.source == Source::kSystem
                              ? commands::DictionaryCostEntry::SYSTEM
                              : commands::DictionaryCostEntry::USER);
  destination->set_reading(source.token.key);
  destination->set_value(source.token.value);
  destination->set_cost(source.token.cost);
  destination->set_lid(source.token.lid);
  destination->set_rid(source.token.rid);
  destination->set_attributes(source.token.attributes);
}

// Exact protobuf wire size of one repeated entry (tag + length + data).
std::size_t EntryWireSize(const dictionary::DictionaryCostEntry& entry) {
  commands::DictionaryCostLookupResult single;
  CopyEntry(entry, single.add_entries());
  return single.ByteSizeLong();
}

struct CandidateSize {
  std::size_t bytes;
  bool system;
  bool included = false;
};

}  // namespace

void BuildBoundedDictionaryCostResponse(
    const dictionary::DictionaryCostLookupResult& raw,
    commands::DictionaryCostLookupResult* output) {
  if (output == nullptr) {
    return;
  }
  output->Clear();
  output->set_system_truncated(raw.system_truncated);
  output->set_user_truncated(raw.user_truncated);

  constexpr std::size_t kMax = commands::kDictionaryCostMaxResponseBytes;
  const std::size_t capacity = kMax - output->ByteSizeLong();

  std::vector<CandidateSize> candidates;
  candidates.reserve(raw.entries.size());
  for (const auto& entry : raw.entries) {
    candidates.push_back(
        {EntryWireSize(entry), entry.source == Source::kSystem, false});
  }

  // First pass: each dictionary has a protected half of the wire budget.
  // A single oversized entry does not consume any of that reservation.
  const std::size_t system_budget = capacity / 2;
  const std::size_t user_budget = capacity - system_budget;
  std::size_t system_used = 0;
  std::size_t user_used = 0;
  std::size_t total_used = 0;
  for (auto& candidate : candidates) {
    std::size_t& used = candidate.system ? system_used : user_used;
    const std::size_t budget = candidate.system ? system_budget : user_budget;
    if (candidate.bytes <= budget - used) {
      candidate.included = true;
      used += candidate.bytes;
      total_used += candidate.bytes;
    }
  }

  // Second pass: reclaim *actual* unused bytes, including reservations left
  // unused when a large entry could not fit in its protected half. This pass
  // never evicts entries accepted by the first pass.
  for (auto& candidate : candidates) {
    if (!candidate.included && candidate.bytes <= capacity - total_used) {
      candidate.included = true;
      total_used += candidate.bytes;
    }
  }

  // Wire size is additive for repeated entries; writing in the original order
  // preserves source order independently of the two selection passes.
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    if (candidates[i].included) {
      CopyEntry(raw.entries[i], output->add_entries());
    } else if (candidates[i].system) {
      output->set_system_truncated(true);
    } else {
      output->set_user_truncated(true);
    }
  }
}

}  // namespace mozc::engine::internal
