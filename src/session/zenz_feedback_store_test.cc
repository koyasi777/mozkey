#include "session/zenz_feedback_store.h"

#include <fstream>
#include <string>
#include <vector>

#include "testing/gunit.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mozc {
namespace session {
namespace {

#if defined(_WIN32)

std::wstring JoinPath(const std::wstring& lhs, const std::wstring& rhs) {
  if (lhs.empty()) {
    return rhs;
  }
  if (lhs.back() == L'\\') {
    return lhs + rhs;
  }
  return lhs + L"\\" + rhs;
}

bool EnsureDirectory(const std::wstring& path) {
  if (::CreateDirectoryW(path.c_str(), nullptr)) {
    return true;
  }
  return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

class ScopedUserProfileForZenzFeedbackStoreTest {
 public:
  ScopedUserProfileForZenzFeedbackStoreTest() {
    wchar_t old_profile[32767] = {};
    const DWORD old_len =
        ::GetEnvironmentVariableW(L"USERPROFILE", old_profile, 32767);
    if (old_len > 0 && old_len < 32767) {
      has_old_profile_ = true;
      old_profile_.assign(old_profile, old_len);
    }

    wchar_t temp_path[MAX_PATH] = {};
    const DWORD temp_len = ::GetTempPathW(MAX_PATH, temp_path);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
      return;
    }

    profile_dir_ =
        std::wstring(temp_path, temp_len) +
        L"mozc_zenz_feedback_store_test_" +
        std::to_wstring(::GetCurrentProcessId()) + L"_" +
        std::to_wstring(::GetTickCount64());

    const std::wstring app_data_dir = JoinPath(profile_dir_, L"AppData");
    const std::wstring local_low_dir = JoinPath(app_data_dir, L"LocalLow");

    if (!EnsureDirectory(profile_dir_) ||
        !EnsureDirectory(app_data_dir) ||
        !EnsureDirectory(local_low_dir)) {
      return;
    }

    ok_ = ::SetEnvironmentVariableW(L"USERPROFILE", profile_dir_.c_str());
  }

  ~ScopedUserProfileForZenzFeedbackStoreTest() {
    if (has_old_profile_) {
      ::SetEnvironmentVariableW(L"USERPROFILE", old_profile_.c_str());
    } else {
      ::SetEnvironmentVariableW(L"USERPROFILE", nullptr);
    }

    const std::wstring app_data_dir = JoinPath(profile_dir_, L"AppData");
    const std::wstring local_low_dir = JoinPath(app_data_dir, L"LocalLow");
    const std::wstring mozc_dir = JoinPath(local_low_dir, L"Mozc");
    const std::wstring feedback_path = JoinPath(mozc_dir, L"zenz_feedback.tsv");

    ::DeleteFileW(feedback_path.c_str());
    ::RemoveDirectoryW(mozc_dir.c_str());
    ::RemoveDirectoryW(local_low_dir.c_str());
    ::RemoveDirectoryW(app_data_dir.c_str());
    ::RemoveDirectoryW(profile_dir_.c_str());
  }

  bool ok() const { return ok_; }

  std::wstring feedback_path() const {
    const std::wstring app_data_dir = JoinPath(profile_dir_, L"AppData");
    const std::wstring local_low_dir = JoinPath(app_data_dir, L"LocalLow");
    const std::wstring mozc_dir = JoinPath(local_low_dir, L"Mozc");
    return JoinPath(mozc_dir, L"zenz_feedback.tsv");
  }

  std::wstring temp_file_path(const std::wstring& name) const {
    return JoinPath(profile_dir_, name);
  }

 private:
  bool ok_ = false;
  bool has_old_profile_ = false;
  std::wstring old_profile_;
  std::wstring profile_dir_;
};

TEST(ZenzFeedbackStoreTest,
     DistinguishesReadingPreservedAcceptedFeedbackFromLegacyAcceptedRows) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "empty", "彼は天敵です");

  std::vector<ZenzFeedbackCandidate> candidates =
      store.GetRankedCandidates("かれはてんてきです", "empty");
  ASSERT_EQ(candidates.size(), 1);
  EXPECT_TRUE(candidates[0].reading_preserved);

  const std::wstring import_path =
      profile.temp_file_path(L"legacy_unvalidated.tsv");
  {
    std::ofstream file(import_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file);
    file << "v2\taccepted\tかぶしき\tempty\t株式会社\treading_preserved\n";
  }

  ASSERT_TRUE(store.ImportFromFile(
      import_path, ZenzFeedbackImportMode::kReplace));

  candidates = store.GetRankedCandidates("かぶしき", "empty");
  ASSERT_EQ(candidates.size(), 1);
  // Import files are not trusted to assert the validation provenance marker.
  EXPECT_FALSE(candidates[0].reading_preserved);
}

TEST(ZenzFeedbackStoreTest, GetAcceptedCandidatesAllowsSingleAcceptedAndSorts) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  // One accepted record is enough and should be returned.
  store.RecordAccepted("k", "empty", "弱い");

  // accepted=2, rejected=0, margin=2. Returned.
  store.RecordAccepted("k", "empty", "強い");
  store.RecordAccepted("k", "empty", "強い");

  // accepted=3, rejected=1, margin=2. Returned and sorted before "強い"
  // because accepted_count is larger.
  store.RecordAccepted("k", "empty", "さらに強い");
  store.RecordAccepted("k", "empty", "さらに強い");
  store.RecordAccepted("k", "empty", "さらに強い");
  store.RecordRejected("k", "empty", "さらに強い", "explicit_reject");

  // Ordinary rejected feedback is a ranking signal, not a hard exclusion.
  // accepted=2 and two medium rejections still produce a positive score, so the
  // candidate remains available but is sorted below stronger candidates.
  store.RecordAccepted("k", "empty", "拒否優勢");
  store.RecordAccepted("k", "empty", "拒否優勢");
  store.RecordRejected("k", "empty", "拒否優勢", "explicit_reject");
  store.RecordRejected("k", "empty", "拒否優勢", "explicit_reject");

  // Different key must be ignored.
  store.RecordAccepted("other", "empty", "別キー");
  store.RecordAccepted("other", "empty", "別キー");
  store.RecordAccepted("other", "empty", "別キー");

  // Sensitive-like context must never be used for normal candidate promotion,
  // even if it has many accepted records.
  store.RecordAccepted("k", "sensitive_like", "機密文脈");
  store.RecordAccepted("k", "sensitive_like", "機密文脈");
  store.RecordAccepted("k", "sensitive_like", "機密文脈");

  const std::vector<ZenzFeedbackCandidate> candidates =
      store.GetAcceptedCandidates("k", "empty");

  ASSERT_EQ(candidates.size(), 4);

  EXPECT_EQ(candidates[0].value, "さらに強い");
  EXPECT_EQ(candidates[0].accepted_count, 3);
  EXPECT_EQ(candidates[0].rejected_count, 1);
  EXPECT_EQ(candidates[0].reason, "feedback_preferred");

  EXPECT_EQ(candidates[1].value, "強い");
  EXPECT_EQ(candidates[1].accepted_count, 2);
  EXPECT_EQ(candidates[1].rejected_count, 0);
  EXPECT_EQ(candidates[1].reason, "feedback_preferred");

  EXPECT_EQ(candidates[2].value, "拒否優勢");
  EXPECT_EQ(candidates[2].accepted_count, 2);
  EXPECT_EQ(candidates[2].rejected_count, 2);
  EXPECT_EQ(candidates[2].reason, "feedback_preferred");
  EXPECT_GT(candidates[2].total_score, 0);

  EXPECT_EQ(candidates[3].value, "弱い");
  EXPECT_EQ(candidates[3].accepted_count, 1);
  EXPECT_EQ(candidates[3].rejected_count, 0);
  EXPECT_EQ(candidates[3].reason, "feedback_preferred");
}

TEST(ZenzFeedbackStoreTest, DecideTreatsOrdinaryRejectedAsSoftSignal) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordRejected("k", "empty", "v", "space_revert_zenz_to_mozc");

  ZenzFeedbackDecision decision = store.Decide("k", "empty", "v");
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kNeutral);
  EXPECT_EQ(decision.reason, "feedback_downgraded");
  EXPECT_EQ(decision.accepted_count, 0);
  EXPECT_EQ(decision.rejected_count, 1);
  EXPECT_LT(decision.total_score, 0);

  store.RecordAccepted("k", "empty", "v");
  decision = store.Decide("k", "empty", "v");
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kPrefer);
  EXPECT_EQ(decision.reason, "feedback_preferred");
  EXPECT_EQ(decision.accepted_count, 1);
  EXPECT_EQ(decision.rejected_count, 1);
  EXPECT_GT(decision.total_score, 0);
}

TEST(ZenzFeedbackStoreTest,
     DecideAutoBlockPolicySuppressesAfterThresholdDynamically) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("k", "empty", "v");
  store.RecordAccepted("k", "empty", "v");
  store.RecordAccepted("k", "empty", "v");
  store.RecordRejected("k", "empty", "v", "space_revert_zenz_to_mozc");
  store.RecordRejected("k", "empty", "v", "space_revert_zenz_to_mozc");

  ZenzFeedbackAutoBlockPolicy policy;
  policy.enabled = true;
  policy.reject_threshold = 2;

  ZenzFeedbackDecision decision = store.Decide("k", "empty", "v", policy);
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kReject);
  EXPECT_EQ(decision.reason, "feedback_auto_blocked");
  EXPECT_TRUE(decision.auto_blocked);
  EXPECT_FALSE(decision.hard_rejected);
  EXPECT_EQ(decision.accepted_count, 3);
  EXPECT_EQ(decision.rejected_count, 2);
  EXPECT_EQ(decision.auto_block_reject_count, 2);

  policy.reject_threshold = 3;
  decision = store.Decide("k", "empty", "v", policy);
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kPrefer);
  EXPECT_EQ(decision.reason, "feedback_preferred");
  EXPECT_FALSE(decision.auto_blocked);

  policy.enabled = false;
  policy.reject_threshold = 1;
  decision = store.Decide("k", "empty", "v", policy);
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kPrefer);
  EXPECT_EQ(decision.reason, "feedback_preferred");
  EXPECT_FALSE(decision.auto_blocked);
}

TEST(ZenzFeedbackStoreTest,
     DecideStopsPreferenceWhenOrdinaryRejectCountDominates) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("k", "empty", "v");
  store.RecordRejected("k", "empty", "v", "space_revert_zenz_to_mozc");
  store.RecordRejected("k", "empty", "v", "space_revert_zenz_to_mozc");

  ZenzFeedbackDecision decision = store.Decide("k", "empty", "v");
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kNeutral);
  EXPECT_EQ(decision.reason, "feedback_reject_count_dominant");
  EXPECT_FALSE(decision.auto_blocked);
  EXPECT_FALSE(decision.hard_rejected);
  EXPECT_EQ(decision.accepted_count, 1);
  EXPECT_EQ(decision.rejected_count, 2);
  EXPECT_EQ(decision.auto_block_reject_count, 2);
  EXPECT_GT(decision.total_score, 0);

  EXPECT_TRUE(store.GetRankedCandidates("k", "empty").empty());

  const std::vector<ZenzFeedbackEntry> entries = store.ListEntries();
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].reason, "feedback_reject_count_dominant");
}

TEST(ZenzFeedbackStoreTest, AutoBlockPolicyUsesExactContextClass) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("k", "japanese_only", "v");
  store.RecordRejected("k", "japanese_only", "v",
                       "space_revert_zenz_to_mozc");
  store.RecordRejected("k", "japanese_only", "v",
                       "space_revert_zenz_to_mozc");

  ZenzFeedbackAutoBlockPolicy policy;
  policy.enabled = true;
  policy.reject_threshold = 2;

  ZenzFeedbackDecision decision = store.Decide("k", "empty", "v", policy);
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kPrefer);
  EXPECT_EQ(decision.reason, "feedback_preferred");
  EXPECT_FALSE(decision.auto_blocked);
  EXPECT_EQ(decision.auto_block_reject_count, 0);

  decision = store.Decide("k", "japanese_only", "v", policy);
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kReject);
  EXPECT_EQ(decision.reason, "feedback_auto_blocked");
  EXPECT_TRUE(decision.auto_blocked);
  EXPECT_EQ(decision.auto_block_reject_count, 2);
}

TEST(ZenzFeedbackStoreTest, DecideAllowsFutureHardRejectReason) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordRejected("k", "empty", "v", "hard_reject");

  ZenzFeedbackDecision decision = store.Decide("k", "empty", "v");
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kReject);
  EXPECT_EQ(decision.reason, "feedback_hard_rejected");
  EXPECT_TRUE(decision.hard_rejected);
}

TEST(ZenzFeedbackStoreTest, DecideHardRejectOverridesAcceptedFeedback) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("k", "empty", "v");
  store.RecordAccepted("k", "empty", "v");
  store.RecordAccepted("k", "empty", "v");
  store.RecordRejected("k", "empty", "v", "hard_reject");

  const ZenzFeedbackDecision decision = store.Decide("k", "empty", "v");
  EXPECT_EQ(decision.action, ZenzFeedbackAction::kReject);
  EXPECT_EQ(decision.reason, "feedback_hard_rejected");
  EXPECT_TRUE(decision.hard_rejected);
  EXPECT_GT(decision.total_score, 0);
}

TEST(ZenzFeedbackStoreTest, GetRankedCandidatesExcludesHardRejectedValue) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("k", "empty", "kept");
  store.RecordAccepted("k", "empty", "blocked");
  store.RecordAccepted("k", "empty", "blocked");
  store.RecordAccepted("k", "empty", "blocked");
  store.RecordRejected("k", "empty", "blocked", "hard_reject");

  const std::vector<ZenzFeedbackCandidate> candidates =
      store.GetRankedCandidates("k", "empty");

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].value, "kept");
  EXPECT_FALSE(candidates[0].hard_rejected);
}

TEST(ZenzFeedbackStoreTest,
     GetRankedCandidatesExcludesAutoBlockedValueOnlyWhenPolicyEnabled) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("k", "empty", "kept");
  store.RecordAccepted("k", "empty", "blocked");
  store.RecordAccepted("k", "empty", "blocked");
  store.RecordAccepted("k", "empty", "blocked");
  store.RecordRejected("k", "empty", "blocked",
                       "space_revert_zenz_to_mozc");
  store.RecordRejected("k", "empty", "blocked",
                       "space_revert_zenz_to_mozc");

  ZenzFeedbackAutoBlockPolicy policy;
  policy.enabled = true;
  policy.reject_threshold = 2;

  std::vector<ZenzFeedbackCandidate> candidates =
      store.GetRankedCandidates("k", "empty", policy);

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].value, "kept");

  policy.enabled = false;
  candidates = store.GetRankedCandidates("k", "empty", policy);

  ASSERT_EQ(candidates.size(), 2);
  EXPECT_EQ(candidates[0].value, "blocked");
  EXPECT_EQ(candidates[1].value, "kept");
}

TEST(ZenzFeedbackStoreTest,
     GetRankedCandidatesExcludesRejectDominantValueWithoutAutoBlock) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted("k", "empty", "kept");
  store.RecordAccepted("k", "empty", "dominant");
  store.RecordRejected("k", "empty", "dominant",
                       "space_revert_zenz_to_mozc");
  store.RecordRejected("k", "empty", "dominant",
                       "space_revert_zenz_to_mozc");

  ZenzFeedbackAutoBlockPolicy policy;
  policy.enabled = false;
  policy.reject_threshold = 1;

  const std::vector<ZenzFeedbackCandidate> candidates =
      store.GetRankedCandidates("k", "empty", policy);

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].value, "kept");
}

TEST(ZenzFeedbackStoreTest,
     GetAcceptedCandidatesSharesSafeContextClassesForPromotion) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  // Feedback learned with a safe Japanese context must still help ordinary
  // conversion where the current lookup context is empty.  Otherwise a useful
  // Zenz correction disappears simply because the later conversion has no
  // preceding text.
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");

  const std::vector<ZenzFeedbackCandidate> empty_candidates =
      store.GetAcceptedCandidates("かれはてんてきです", "empty");

  ASSERT_EQ(empty_candidates.size(), 1);
  EXPECT_EQ(empty_candidates[0].value, "彼は天敵です");
  EXPECT_EQ(empty_candidates[0].accepted_count, 1);
  EXPECT_EQ(empty_candidates[0].rejected_count, 0);
  EXPECT_EQ(empty_candidates[0].reason, "feedback_preferred");

  const std::vector<ZenzFeedbackCandidate> symbol_candidates =
      store.GetAcceptedCandidates("かれはてんてきです", "symbol_or_other");

  ASSERT_EQ(symbol_candidates.size(), 1);
  EXPECT_EQ(symbol_candidates[0].value, "彼は天敵です");
  EXPECT_EQ(symbol_candidates[0].accepted_count, 1);
  EXPECT_EQ(symbol_candidates[0].rejected_count, 0);
  EXPECT_EQ(symbol_candidates[0].reason, "feedback_preferred");
}

TEST(ZenzFeedbackStoreTest,
     GetAcceptedCandidatesAggregatesSafeContextClassesByValue) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("かれはてんてきです", "empty", "彼は天敵です");
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");
  store.RecordAccepted("かれはてんてきです", "japanese_with_punctuation",
                       "彼は天敵です");
  store.RecordRejected("かれはてんてきです", "empty", "彼は天敵です",
                       "explicit_reject");

  const std::vector<ZenzFeedbackCandidate> candidates =
      store.GetAcceptedCandidates("かれはてんてきです", "empty");

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].value, "彼は天敵です");
  EXPECT_EQ(candidates[0].accepted_count, 3);
  EXPECT_EQ(candidates[0].rejected_count, 1);
  EXPECT_EQ(candidates[0].reason, "feedback_preferred");
}

TEST(ZenzFeedbackStoreTest,
     GetAcceptedCandidatesDoesNotShareSensitiveLikeContext) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("secret_key", "sensitive_like", "秘密候補");
  store.RecordAccepted("secret_key", "sensitive_like", "秘密候補");
  store.RecordAccepted("secret_key", "sensitive_like", "秘密候補");

  EXPECT_TRUE(
      store.GetAcceptedCandidates("secret_key", "empty").empty());
  EXPECT_TRUE(
      store.GetAcceptedCandidates("secret_key", "japanese_only").empty());
  EXPECT_TRUE(
      store.GetAcceptedCandidates("secret_key", "symbol_or_other").empty());
}

TEST(ZenzFeedbackStoreTest,
     GetAcceptedCandidatesAllowsSensitiveLikeExactMatch) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;
  store.RecordAccepted(
      "はげんざいこうですが",
      "sensitive_like",
      "は現在こうですが");

  const std::vector<ZenzFeedbackCandidate> candidates =
      store.GetAcceptedCandidates(
          "はげんざいこうですが",
          "sensitive_like");

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].value, "は現在こうですが");
  EXPECT_EQ(candidates[0].accepted_count, 1);
  EXPECT_EQ(candidates[0].rejected_count, 0);

  const std::vector<ZenzFeedbackCandidate> normal_candidates =
      store.GetAcceptedCandidates(
          "はげんざいこうですが",
          "empty");

  EXPECT_TRUE(normal_candidates.empty());
}

TEST(ZenzFeedbackStoreTest, GetAcceptedCandidatesAcceptsUtf8Bom) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  // Create %USERPROFILE%\AppData\LocalLow\Mozc first.  This test intentionally
  // overwrites the TSV directly to simulate a file saved by tools/editors that
  // write UTF-8 with BOM.
  store.RecordAccepted("__mkdir__", "empty", "__mkdir__");

  {
    std::ofstream file(profile.feedback_path(),
                       std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file);
    file << "\xEF\xBB\xBF"
         << "v2\taccepted\tかれはてんてきです\tjapanese_only\t彼は天敵です\t\n";
  }

  const std::vector<ZenzFeedbackCandidate> candidates =
      store.GetAcceptedCandidates("かれはてんてきです", "empty");

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].value, "彼は天敵です");
  EXPECT_EQ(candidates[0].accepted_count, 1);
  EXPECT_EQ(candidates[0].rejected_count, 0);
  EXPECT_EQ(candidates[0].reason, "feedback_preferred");
}

TEST(ZenzFeedbackStoreTest, GetAcceptedCandidatesNormalizesEmptyContextClass) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("empty_context_key", "", "空文脈候補");
  store.RecordAccepted("empty_context_key", "", "空文脈候補");

  const std::vector<ZenzFeedbackCandidate> candidates_from_empty =
      store.GetAcceptedCandidates("empty_context_key", "");
  const std::vector<ZenzFeedbackCandidate> candidates_from_empty_label =
      store.GetAcceptedCandidates("empty_context_key", "empty");

  ASSERT_EQ(candidates_from_empty.size(), 1);
  EXPECT_EQ(candidates_from_empty[0].value, "空文脈候補");
  EXPECT_EQ(candidates_from_empty[0].accepted_count, 2);
  EXPECT_EQ(candidates_from_empty[0].rejected_count, 0);

  ASSERT_EQ(candidates_from_empty_label.size(), 1);
  EXPECT_EQ(candidates_from_empty_label[0].value, "空文脈候補");
  EXPECT_EQ(candidates_from_empty_label[0].accepted_count, 2);
  EXPECT_EQ(candidates_from_empty_label[0].rejected_count, 0);
}

TEST(ZenzFeedbackStoreTest, ListEntriesAggregatesExactEntries) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("b", "empty", "B");
  store.RecordRejected("b", "empty", "B", "explicit_reject");
  store.RecordAccepted("a", "japanese_only", "A");

  const std::vector<ZenzFeedbackEntry> entries = store.ListEntries();

  ASSERT_EQ(entries.size(), 2);

  EXPECT_EQ(entries[0].key, "a");
  EXPECT_EQ(entries[0].context_class, "japanese_only");
  EXPECT_EQ(entries[0].value, "A");
  EXPECT_EQ(entries[0].accepted_count, 1);
  EXPECT_EQ(entries[0].rejected_count, 0);
  EXPECT_EQ(entries[0].reason, "feedback_preferred");

  EXPECT_EQ(entries[1].key, "b");
  EXPECT_EQ(entries[1].context_class, "empty");
  EXPECT_EQ(entries[1].value, "B");
  EXPECT_EQ(entries[1].accepted_count, 1);
  EXPECT_EQ(entries[1].rejected_count, 1);
  EXPECT_EQ(entries[1].reason, "feedback_preferred");
  EXPECT_GT(store.Decide("b", "empty", "B").total_score, 0);
}

TEST(ZenzFeedbackStoreTest,
     FullSequenceFeedbackDoesNotMatchSegmentLocalLookup) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  // ZenzFeedbackStore is scoped to the complete Zenz reading/correction pair.
  // Recording a full-sequence observation must not implicitly create
  // segment-local or lexical-unit feedback.
  store.RecordAccepted("full_sequence_reading", "japanese_only",
                       "full_sequence_correction");
  store.RecordRejected("full_sequence_reading", "japanese_only",
                       "full_sequence_correction",
                       "space_revert_zenz_to_mozc");

  const std::vector<ZenzFeedbackCandidate> full_candidates =
      store.GetRankedCandidates("full_sequence_reading", "empty");
  ASSERT_EQ(full_candidates.size(), 1);
  EXPECT_EQ(full_candidates[0].value, "full_sequence_correction");
  EXPECT_EQ(full_candidates[0].accepted_count, 1);
  EXPECT_EQ(full_candidates[0].rejected_count, 1);
  EXPECT_GT(full_candidates[0].total_score, 0);

  EXPECT_TRUE(store.GetRankedCandidates("segment_reading", "empty").empty());
  EXPECT_EQ(store.Decide("segment_reading", "empty", "segment_correction")
                .action,
            ZenzFeedbackAction::kNeutral);

  const std::vector<ZenzFeedbackEntry> entries = store.ListEntries();
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].key, "full_sequence_reading");
  EXPECT_EQ(entries[0].context_class, "japanese_only");
  EXPECT_EQ(entries[0].value, "full_sequence_correction");
  EXPECT_EQ(entries[0].accepted_count, 1);
  EXPECT_EQ(entries[0].rejected_count, 1);
}

TEST(ZenzFeedbackStoreTest, DeleteEntryRemovesOnlyMatchingRawRecords) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("k", "empty", "削除対象");
  store.RecordAccepted("k", "empty", "削除対象");
  store.RecordRejected("k", "empty", "削除対象", "explicit_reject");
  store.RecordAccepted("k", "empty", "残す");
  store.RecordAccepted("k", "japanese_only", "削除対象");

  EXPECT_TRUE(store.DeleteEntry("k", "empty", "削除対象"));

  const std::vector<ZenzFeedbackEntry> entries = store.ListEntries();

  ASSERT_EQ(entries.size(), 2);

  auto find_entry = [&](const char* key,
                        const char* context_class,
                        const char* value) -> const ZenzFeedbackEntry* {
    for (const ZenzFeedbackEntry& entry : entries) {
      if (entry.key == key &&
          entry.context_class == context_class &&
          entry.value == value) {
        return &entry;
      }
    }
    return nullptr;
  };

  EXPECT_EQ(find_entry("k", "empty", "削除対象"), nullptr);

  const ZenzFeedbackEntry* empty_remaining =
      find_entry("k", "empty", "残す");
  ASSERT_NE(empty_remaining, nullptr);
  EXPECT_EQ(empty_remaining->accepted_count, 1);
  EXPECT_EQ(empty_remaining->rejected_count, 0);
  EXPECT_EQ(empty_remaining->reason, "feedback_preferred");

  const ZenzFeedbackEntry* japanese_only_remaining =
      find_entry("k", "japanese_only", "削除対象");
  ASSERT_NE(japanese_only_remaining, nullptr);
  EXPECT_EQ(japanese_only_remaining->accepted_count, 1);
  EXPECT_EQ(japanese_only_remaining->rejected_count, 0);
  EXPECT_EQ(japanese_only_remaining->reason, "feedback_preferred");
}

TEST(ZenzFeedbackStoreTest, ClearAllRemovesAllEntries) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("k", "empty", "v");
  ASSERT_FALSE(store.ListEntries().empty());

  EXPECT_TRUE(store.ClearAll());
  EXPECT_TRUE(store.ListEntries().empty());

  std::ifstream file(profile.feedback_path(), std::ios::binary);
  EXPECT_FALSE(file);
}

TEST(ZenzFeedbackStoreTest, ExportAndImportReplacePreservesRecords) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("k", "empty", "v");
  store.RecordRejected("k", "empty", "v", "explicit_reject");

  const std::wstring export_path = profile.temp_file_path(L"export.tsv");
  ASSERT_TRUE(store.ExportToFile(export_path));

  ASSERT_TRUE(store.ClearAll());
  ASSERT_TRUE(store.ListEntries().empty());

  ASSERT_TRUE(store.ImportFromFile(
      export_path, ZenzFeedbackImportMode::kReplace));

  const std::vector<ZenzFeedbackEntry> entries = store.ListEntries();

  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].key, "k");
  EXPECT_EQ(entries[0].context_class, "empty");
  EXPECT_EQ(entries[0].value, "v");
  EXPECT_EQ(entries[0].accepted_count, 1);
  EXPECT_EQ(entries[0].rejected_count, 1);
  EXPECT_EQ(entries[0].reason, "feedback_preferred");
  EXPECT_GT(store.Decide("k", "empty", "v").total_score, 0);
}

TEST(ZenzFeedbackStoreTest, ImportAppendKeepsExistingRecords) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("existing", "empty", "既存");

  const std::wstring import_path = profile.temp_file_path(L"import_append.tsv");
  {
    std::ofstream file(import_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file);
    file << "v2\taccepted\timported\tempty\t追加\t\n";
  }

  ASSERT_TRUE(store.ImportFromFile(
      import_path, ZenzFeedbackImportMode::kAppend));

  const std::vector<ZenzFeedbackEntry> entries = store.ListEntries();

  ASSERT_EQ(entries.size(), 2);
  EXPECT_EQ(entries[0].key, "existing");
  EXPECT_EQ(entries[0].value, "既存");
  EXPECT_EQ(entries[1].key, "imported");
  EXPECT_EQ(entries[1].value, "追加");
}

TEST(ZenzFeedbackStoreTest, ImportRejectsMalformedFileWithoutChangingExisting) {
  ScopedUserProfileForZenzFeedbackStoreTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzFeedbackStore store;

  store.RecordAccepted("existing", "empty", "既存");

  const std::wstring import_path =
      profile.temp_file_path(L"import_malformed.tsv");
  {
    std::ofstream file(import_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file);
    file << "v2\tunknown_action\tk\tempty\tv\t\n";
  }

  EXPECT_FALSE(store.ImportFromFile(
      import_path, ZenzFeedbackImportMode::kAppend));

  const std::vector<ZenzFeedbackEntry> entries = store.ListEntries();

  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].key, "existing");
  EXPECT_EQ(entries[0].context_class, "empty");
  EXPECT_EQ(entries[0].value, "既存");
  EXPECT_EQ(entries[0].accepted_count, 1);
  EXPECT_EQ(entries[0].rejected_count, 0);
}

#else  // defined(_WIN32)

TEST(ZenzFeedbackStoreTest, SkippedOnNonWindows) {
  GTEST_SKIP() << "ZenzFeedbackStore persists to LocalLow on Windows.";
}

#endif  // defined(_WIN32)

}  // namespace
}  // namespace session
}  // namespace mozc
