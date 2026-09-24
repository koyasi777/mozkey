#ifndef MOZC_SESSION_ZENZ_FEEDBACK_STORE_H_
#define MOZC_SESSION_ZENZ_FEEDBACK_STORE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

enum class ZenzFeedbackAction {
  kNeutral = 0,
  kPrefer = 1,
  kReject = 2,
};

enum class ZenzFeedbackImportMode {
  kAppend = 0,
  kReplace = 1,
};

struct ZenzFeedbackAutoBlockPolicy {
  bool enabled = false;
  int minimum_reject_count = 0;
  int minimum_reject_percentage = 0;
};

struct ZenzFeedbackDecision {
  ZenzFeedbackAction action = ZenzFeedbackAction::kNeutral;
  std::string reason = "feedback_neutral";
  int accepted_count = 0;
  int rejected_count = 0;
  int positive_score = 0;
  int negative_score = 0;
  int total_score = 0;
  int auto_block_reject_count = 0;
  bool hard_rejected = false;
  bool auto_blocked = false;
};

struct ZenzFeedbackCandidate {
  std::string value;
  int accepted_count = 0;
  int rejected_count = 0;
  int positive_score = 0;
  int negative_score = 0;
  int total_score = 0;
  int auto_block_reject_count = 0;
  bool hard_rejected = false;
  bool auto_blocked = false;
  std::string reason = "feedback_neutral";
};

struct ZenzFeedbackEntry {
  std::string key;
  std::string context_class;
  std::string value;
  int accepted_count = 0;
  int rejected_count = 0;
  int auto_block_reject_count = 0;
  int auto_block_reject_percentage = 0;
  bool hard_rejected = false;
  bool auto_blocked = false;
  std::string reason = "feedback_neutral";
};

// Persistent local feedback for Zenz live correction.
//
// Scope is intentionally full-sequence only:
//   key   = the complete reading submitted to Zenz
//   value = the complete validated Zenz correction observed for that request
//
// This store does not own segment-local or lexical-unit learning.  If an
// accepted full-sequence correction is later decomposed into safe local units,
// those units belong to Mozc history/user-segment learning, not to this TSV.
// The context dimension is also a coarse, non-reversible class.  Raw left
// context must never be persisted here.
class ZenzFeedbackStore {
 public:
  ZenzFeedbackDecision Decide(absl::string_view key,
                              absl::string_view context_class,
                              absl::string_view value) const;

  ZenzFeedbackDecision Decide(
      absl::string_view key,
      absl::string_view context_class,
      absl::string_view value,
      const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const;

  // Returns feedback-scored values for the given key/context_class.
  //
  // Compatible non-sensitive context classes are aggregated, as in Decide().
  // Sensitive-like feedback is not promoted across context classes.
  //
  // The returned candidates are not a hard accepted/rejected binary list.
  // Accepted feedback contributes positive score. Ordinary rejected feedback
  // contributes a reason-dependent negative score and should be interpreted as
  // a ranking signal, not as a command to suppress the candidate. Candidates
  // with only negative observations are not returned because they provide no
  // evidence that the value should be inserted into the normal conversion
  // candidate list.
  //
  // The result is sorted by stronger total feedback score first.
  std::vector<ZenzFeedbackCandidate> GetRankedCandidates(
      absl::string_view key,
      absl::string_view context_class) const;

  std::vector<ZenzFeedbackCandidate> GetRankedCandidates(
      absl::string_view key,
      absl::string_view context_class,
      const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const;

  // Compatibility wrapper for older call sites.  Prefer GetRankedCandidates()
  // for new code so rejected feedback can be used as a cost/ranking signal.
  std::vector<ZenzFeedbackCandidate> GetAcceptedCandidates(
      absl::string_view key,
      absl::string_view context_class) const;

  std::vector<ZenzFeedbackCandidate> GetAcceptedCandidates(
      absl::string_view key,
      absl::string_view context_class,
      const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const;

  // Returns exact persisted feedback entries aggregated by
  // key/context_class/value.  Unlike ranked candidate lookup, this method does
  // not merge compatible context classes.  It is intended for management UI.
  std::vector<ZenzFeedbackEntry> ListEntries() const;

  std::vector<ZenzFeedbackEntry> ListEntries(
      const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const;

  // Exports the raw feedback history as normalized v2 UTF-8 TSV.
  [[nodiscard]]
  bool ExportToFile(const std::wstring& path) const;

  // Imports normalized v2 UTF-8 TSV.  Legacy v1 rows are accepted and converted
  // to the non-reversible "legacy" context class.
  [[nodiscard]]
  bool ImportFromFile(const std::wstring& path,
                      ZenzFeedbackImportMode mode);

  // Deletes all raw records matching exactly key/context_class/value.
  [[nodiscard]]
  bool DeleteEntry(absl::string_view key,
                   absl::string_view context_class,
                   absl::string_view value);

  // Removes all persisted zenz feedback data.
  [[nodiscard]]
  bool ClearAll();

  // Records one accepted full-sequence Zenz correction.
  void RecordAccepted(absl::string_view key,
                      absl::string_view context_class,
                      absl::string_view value);

  // Records one rejected full-sequence Zenz correction.  Ordinary rejects are
  // later interpreted as ranking signals; they are not segment-local negatives.
  void RecordRejected(absl::string_view key,
                      absl::string_view context_class,
                      absl::string_view value,
                      absl::string_view reason);
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_FEEDBACK_STORE_H_
