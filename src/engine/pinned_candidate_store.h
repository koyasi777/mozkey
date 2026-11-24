// Copyright 2026 Mozkey contributors.
// Candidate pinning stores explicit, per-reading user preferences, not history.
#ifndef MOZC_ENGINE_PINNED_CANDIDATE_STORE_H_
#define MOZC_ENGINE_PINNED_CANDIDATE_STORE_H_

#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"

namespace mozc::engine {

class PinnedCandidateStore final {
 public:
  explicit PinnedCandidateStore(std::string path);
  PinnedCandidateStore(const PinnedCandidateStore&) = delete;
  PinnedCandidateStore& operator=(const PinnedCandidateStore&) = delete;
  static PinnedCandidateStore& Instance();

  std::optional<std::string> Lookup(absl::string_view reading);
  bool Pin(absl::string_view reading, absl::string_view value);
  // Never remove a newer pin because of a stale candidate-window command.
  bool Unpin(absl::string_view reading, absl::string_view expected_value);

 private:
  // Returns false on any read or parse error; never treat an error as empty.
  bool LoadLocked();
  bool SaveLocked(const std::map<std::string, std::string>& entries);

  std::string path_;
  std::mutex mu_;
  bool loaded_ = false;
  std::map<std::string, std::string> entries_;
};

}  // namespace mozc::engine

#endif  // MOZC_ENGINE_PINNED_CANDIDATE_STORE_H_
