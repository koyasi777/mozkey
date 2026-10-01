// Copyright 2010-2021, Google Inc.
// All rights reserved.

#ifndef MOZC_SESSION_ZENZ_LOCAL_CORRECTION_STORE_H_
#define MOZC_SESSION_ZENZ_LOCAL_CORRECTION_STORE_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc::session {

// One explicit local correction observation.
//
// The key is deliberately contrastive:
//   reading          = the local reading
//   rejected_value   = the exact Zenz surface the user rejected
//   accepted_value   = the local Mozc baseline surface the user finally kept
//
// This is not a dictionary rule saying reading -> accepted_value. Replay is
// permitted only when the same rejected surface recurs and later safety gates
// still agree.
struct ZenzLocalCorrectionEntry {
  std::string reading;
  std::string rejected_value;
  std::string accepted_value;
  int observation_count = 0;
};

class ZenzLocalCorrectionStore {
 public:
  // Records one explicit local correction observation.
  void RecordCorrection(absl::string_view reading,
                        absl::string_view rejected_value,
                        absl::string_view accepted_value);

  // Returns an exact contrastive triple aggregated by observation count.
  //
  // accepted_value is part of the lookup key because every recorded correction
  // is proven against the Mozc baseline that was current for that observation.
  // The same reading/rejected pair may therefore safely coexist with multiple
  // accepted baselines; replay still requires the current Mozc baseline to
  // match the exact accepted_value that was previously observed.
  std::optional<ZenzLocalCorrectionEntry> Lookup(
      absl::string_view reading,
      absl::string_view rejected_value,
      absl::string_view accepted_value) const;

  // Returns exact triples aggregated by observation count.
  std::vector<ZenzLocalCorrectionEntry> ListEntries() const;

  // Removes all persisted observations for one exact contrastive triple.
  // Returns false when the triple no longer exists or persistence fails.
  [[nodiscard]] bool DeleteEntry(absl::string_view reading,
                                 absl::string_view rejected_value,
                                 absl::string_view accepted_value);

  // Removes the separate local-correction TSV.
  [[nodiscard]] bool ClearAll();
};

}  // namespace mozc::session

#endif  // MOZC_SESSION_ZENZ_LOCAL_CORRECTION_STORE_H_