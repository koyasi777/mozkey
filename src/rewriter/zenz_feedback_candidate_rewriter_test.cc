#include "rewriter/zenz_feedback_candidate_rewriter.h"

#include <fstream>
#include <string>
#include <utility>

#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "session/zenz_feedback_store.h"
#include "testing/gunit.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mozc {
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

class ScopedUserProfileForZenzFeedbackCandidateRewriterTest {
 public:
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest() {
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
        L"mozc_zenz_feedback_candidate_rewriter_test_" +
        std::to_wstring(::GetCurrentProcessId()) + L"_" +
        std::to_wstring(static_cast<unsigned long long>(::GetTickCount64()));

    const std::wstring app_data_dir = JoinPath(profile_dir_, L"AppData");
    const std::wstring local_low_dir = JoinPath(app_data_dir, L"LocalLow");

    if (!EnsureDirectory(profile_dir_) ||
        !EnsureDirectory(app_data_dir) ||
        !EnsureDirectory(local_low_dir)) {
      return;
    }

    ok_ = ::SetEnvironmentVariableW(L"USERPROFILE", profile_dir_.c_str());
  }

  ~ScopedUserProfileForZenzFeedbackCandidateRewriterTest() {
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

  std::wstring temp_file_path(const std::wstring& name) const {
    return JoinPath(profile_dir_, name);
  }

 private:
  bool ok_ = false;
  bool has_old_profile_ = false;
  std::wstring old_profile_;
  std::wstring profile_dir_;
};

void AddSegment(absl::string_view key,
                absl::string_view value,
                Segments* segments) {
  Segment* segment = segments->push_back_segment();
  segment->set_key(key);

  converter::Candidate* candidate = segment->add_candidate();
  candidate->key = std::string(key);
  candidate->content_key = std::string(key);
  candidate->value = std::string(value);
  candidate->content_value = std::string(value);
  candidate->attributes = converter::Attribute::BEST_CANDIDATE;
}

ConversionRequest CreateZenzFeedbackConversionRequest(
    absl::string_view key = "かれはてんてきです") {
  config::Config config;
  config.set_use_zenz_feedback_learning(true);
  config.set_history_learning_level(config::Config::DEFAULT_HISTORY);

  ConversionRequest::Options options;
  options.request_type = ConversionRequest::CONVERSION;
  options.enable_user_history_for_conversion = true;
  options.incognito_mode = false;

  return ConversionRequestBuilder()
      .SetConfig(config)
      .SetOptions(std::move(options))
      .SetRequestType(ConversionRequest::CONVERSION)
      .SetKey(key)
      .Build();
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotRewriteMultiSegmentConversionEvenWhenFullKeyHasAcceptedFeedback) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  // Feedback learned for the full key must not collapse Mozc's phrase
  // boundaries.  Phrase boundaries are owned by the converter, not by this
  // feedback rewriter.
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");

  Segments segments;
  AddSegment("かれは", "彼は", &segments);
  AddSegment("てんてきです", "点滴です", &segments);

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "彼は");
  ASSERT_EQ(segments.conversion_segment(1).candidate(0).value, "点滴です");

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_EQ(rewriter.capability(request), RewriterInterface::CONVERSION);
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  EXPECT_EQ(segments.conversion_segment(0).key(), "かれは");
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "彼は");
  EXPECT_EQ(segments.conversion_segment(1).key(), "てんてきです");
  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "点滴です");
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotInsertLegacyUnvalidatedFeedbackAsSyntheticCandidate) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  const std::wstring import_path =
      profile.temp_file_path(L"legacy_unvalidated.tsv");
  {
    std::ofstream file(import_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file);
    file << "v2\taccepted\tかぶしき\tempty\t株式会社\t\n";
  }

  session::ZenzFeedbackStore store;
  ASSERT_TRUE(store.ImportFromFile(
      import_path, session::ZenzFeedbackImportMode::kReplace));

  Segments segments;
  AddSegment("かぶしき", "株式", &segments);

  const ConversionRequest request =
      CreateZenzFeedbackConversionRequest("かぶしき");

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "株式");
}

TEST(ZenzFeedbackCandidateRewriterTest,
     InsertsSingleSegmentConversionByFeedbackAdjustedCost) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  // The rewriter queries feedback with context_class="empty".  Feedback learned
  // while safe left context existed, such as japanese_only, must still promote
  // the same full-key correction in ordinary conversion.
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "彼は点滴です");

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_EQ(rewriter.capability(request), RewriterInterface::CONVERSION);
  EXPECT_TRUE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);

  const Segment& segment = segments.conversion_segment(0);
  EXPECT_EQ(segment.key(), "かれはてんてきです");
  ASSERT_GE(segment.candidates_size(), 2);

  // With the default zero-cost test candidate, one accepted Zenz feedback
  // entry is not allowed to hard-promote itself above the existing Mozc top
  // candidate.  It participates in the same candidate set instead.
  EXPECT_EQ(segment.candidate(0).key, "かれはてんてきです");
  EXPECT_EQ(segment.candidate(0).content_key, "かれはてんてきです");
  EXPECT_EQ(segment.candidate(0).value, "彼は点滴です");
  EXPECT_EQ(segment.candidate(0).content_value, "彼は点滴です");
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);

  EXPECT_EQ(segment.candidate(1).key, "かれはてんてきです");
  EXPECT_EQ(segment.candidate(1).content_key, "かれはてんてきです");
  EXPECT_EQ(segment.candidate(1).value, "彼は天敵です");
  EXPECT_EQ(segment.candidate(1).content_value, "彼は天敵です");
  EXPECT_TRUE(segment.candidate(1).attributes & converter::Attribute::RERANKED);
  EXPECT_TRUE(segment.candidate(1).attributes &
              converter::Attribute::USER_SEGMENT_HISTORY_REWRITER);
  EXPECT_FALSE(segment.candidate(1).attributes &
               converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     PromotesStrongFeedbackWhenAdjustedCostBecomesBest) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->cost = 1000;
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->wcost = 1000;

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_TRUE(rewriter.Rewrite(request, &segments));

  const Segment& segment = segments.conversion_segment(0);
  ASSERT_GE(segment.candidates_size(), 2);
  EXPECT_EQ(segment.candidate(0).value, "彼は天敵です");
  EXPECT_TRUE(segment.candidate(0).attributes & converter::Attribute::RERANKED);
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::USER_SEGMENT_HISTORY_REWRITER);
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
  EXPECT_EQ(segment.candidate(1).value, "彼は点滴です");
  EXPECT_FALSE(segment.candidate(1).attributes &
               converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotRewriteMultiSegmentConversionWithoutFeedback) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  // Multi-segment conversion is outside this rewriter's responsibility.
  // Phrase boundaries must be preserved regardless of feedback availability.

  Segments segments;
  AddSegment("かれは", "彼は", &segments);
  AddSegment("てんてきです", "点滴です", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  EXPECT_EQ(segments.conversion_segment(0).key(), "かれは");
  EXPECT_EQ(segments.conversion_segment(1).key(), "てんてきです");
}

TEST(ZenzFeedbackCandidateRewriterTest,
     RepositionsExistingCandidateByFeedbackAdjustedCost) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);
  Segment* segment = segments.mutable_conversion_segment(0);
  segment->mutable_candidate(0)->cost = 0;
  segment->mutable_candidate(0)->wcost = 0;

  converter::Candidate* existing = segment->add_candidate();
  existing->key = "かれはてんてきです";
  existing->content_key = "かれはてんてきです";
  existing->value = "彼は天敵です";
  existing->content_value = "彼は天敵です";
  existing->cost = 1600;
  existing->wcost = 1600;

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_TRUE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segment->candidates_size(), 2);
  EXPECT_EQ(segment->candidate(0).value, "彼は点滴です");
  EXPECT_TRUE(segment->candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
  EXPECT_EQ(segment->candidate(1).value, "彼は天敵です");
  EXPECT_EQ(segment->candidate(1).cost, 1100);
  EXPECT_TRUE(segment->candidate(1).attributes & converter::Attribute::RERANKED);
  EXPECT_TRUE(segment->candidate(1).attributes &
              converter::Attribute::USER_SEGMENT_HISTORY_REWRITER);
  EXPECT_FALSE(segment->candidate(1).attributes &
               converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotPromoteSingleSegmentConversionWithSensitiveLikeFeedbackOnly) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "sensitive_like", "彼は天敵です");
  store.RecordAccepted("かれはてんてきです", "sensitive_like", "彼は天敵です");
  store.RecordAccepted("かれはてんてきです", "sensitive_like", "彼は天敵です");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);

  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.key(), "かれはてんてきです");
  EXPECT_EQ(segment.candidate(0).key, "かれはてんてきです");
  EXPECT_EQ(segment.candidate(0).value, "彼は点滴です");
}

TEST(ZenzFeedbackCandidateRewriterTest,
     KeepsSoftRejectedFeedbackCandidateButReducesItsBonus) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");
  store.RecordRejected("かれはてんてきです", "japanese_only", "彼は天敵です",
                       "space_revert_zenz_to_mozc");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);
  Segment* segment = segments.mutable_conversion_segment(0);
  segment->mutable_candidate(0)->cost = 0;
  segment->mutable_candidate(0)->wcost = 0;

  converter::Candidate* existing = segment->add_candidate();
  existing->key = "かれはてんてきです";
  existing->content_key = "かれはてんてきです";
  existing->value = "彼は天敵です";
  existing->content_value = "彼は天敵です";
  existing->cost = 1600;
  existing->wcost = 1600;

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_TRUE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segment->candidates_size(), 2);
  EXPECT_EQ(segment->candidate(0).value, "彼は点滴です");
  EXPECT_TRUE(segment->candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);

  // A Space-derived rejection is a soft negative signal.  It reduces the
  // accepted feedback bonus, but it must not delete or hard-suppress the
  // candidate while the total feedback score remains positive.
  EXPECT_EQ(segment->candidate(1).value, "彼は天敵です");
  EXPECT_EQ(segment->candidate(1).cost, 1175);
  EXPECT_EQ(segment->candidate(1).wcost, 1175);
  EXPECT_TRUE(segment->candidate(1).attributes & converter::Attribute::RERANKED);
  EXPECT_TRUE(segment->candidate(1).attributes &
              converter::Attribute::USER_SEGMENT_HISTORY_REWRITER);
  EXPECT_FALSE(segment->candidate(1).attributes &
               converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotInsertOrPromoteHardRejectedFeedbackCandidate) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");
  store.RecordRejected("かれはてんてきです", "japanese_only", "彼は天敵です",
                       "hard_reject");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "彼は点滴です");
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotInsertOrPromoteAutoBlockedFeedbackCandidate) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "empty", "彼は天敵です");
  store.RecordAccepted("かれはてんてきです", "empty", "彼は天敵です");
  store.RecordAccepted("かれはてんてきです", "empty", "彼は天敵です");
  store.RecordRejected("かれはてんてきです", "empty", "彼は天敵です",
                       "space_revert_zenz_to_mozc");
  store.RecordRejected("かれはてんてきです", "empty", "彼は天敵です",
                       "space_revert_zenz_to_mozc");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);

  config::Config config;
  config.set_use_zenz_feedback_learning(true);
  config.set_history_learning_level(config::Config::DEFAULT_HISTORY);
  config.set_use_zenz_auto_block_rejected_correction(true);
  config.set_zenz_auto_block_reject_threshold(2);

  ConversionRequest::Options options;
  options.request_type = ConversionRequest::CONVERSION;
  options.enable_user_history_for_conversion = true;
  options.incognito_mode = false;

  const ConversionRequest request =
      ConversionRequestBuilder()
          .SetConfig(config)
          .SetOptions(std::move(options))
          .SetRequestType(ConversionRequest::CONVERSION)
          .SetKey("かれはてんてきです")
          .Build();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "彼は点滴です");
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotInsertOrPromoteRejectDominantFeedbackCandidate) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "empty", "彼は天敵です");
  store.RecordRejected("かれはてんてきです", "empty", "彼は天敵です",
                       "space_revert_zenz_to_mozc");
  store.RecordRejected("かれはてんてきです", "empty", "彼は天敵です",
                       "space_revert_zenz_to_mozc");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は点滴です", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "彼は点滴です");
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotDuplicateFeedbackWhenMozcTopAlreadyMatches) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("かれはてんてきです", "japanese_only", "彼は天敵です");

  Segments segments;
  AddSegment("かれはてんてきです", "彼は天敵です", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "彼は天敵です");
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     IsNotAvailableWhenUserHistoryCannotBeUsed) {
  config::Config config;
  config.set_use_zenz_feedback_learning(true);
  config.set_history_learning_level(config::Config::DEFAULT_HISTORY);

  ConversionRequest::Options options;
  options.request_type = ConversionRequest::CONVERSION;
  options.enable_user_history_for_conversion = true;
  options.incognito_mode = true;

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_EQ(rewriter.capability(ConversionRequestBuilder()
                                    .SetConfig(config)
                                    .SetOptions(options)
                                    .SetRequestType(ConversionRequest::CONVERSION)
                                    .SetKey("かれはてんてきです")
                                    .Build()),
            RewriterInterface::NOT_AVAILABLE);

  options.incognito_mode = false;
  options.enable_user_history_for_conversion = false;
  EXPECT_EQ(rewriter.capability(ConversionRequestBuilder()
                                    .SetConfig(config)
                                    .SetOptions(options)
                                    .SetRequestType(ConversionRequest::CONVERSION)
                                    .SetKey("かれはてんてきです")
                                    .Build()),
            RewriterInterface::NOT_AVAILABLE);

  options.enable_user_history_for_conversion = true;
  config.set_history_learning_level(config::Config::NO_HISTORY);
  EXPECT_EQ(rewriter.capability(ConversionRequestBuilder()
                                    .SetConfig(config)
                                    .SetOptions(options)
                                    .SetRequestType(ConversionRequest::CONVERSION)
                                    .SetKey("かれはてんてきです")
                                    .Build()),
            RewriterInterface::NOT_AVAILABLE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     IsNotAvailableWhenZenzFeedbackLearningIsDisabled) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  config::Config config;
  config.set_use_zenz_feedback_learning(false);
  config.set_history_learning_level(config::Config::DEFAULT_HISTORY);

  ConversionRequest::Options options;
  options.request_type = ConversionRequest::CONVERSION;
  options.enable_user_history_for_conversion = true;

  const ConversionRequest request =
      ConversionRequestBuilder()
          .SetConfig(config)
          .SetOptions(std::move(options))
          .SetRequestType(ConversionRequest::CONVERSION)
          .SetKey("かれはてんてきです")
          .Build();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_EQ(rewriter.capability(request), RewriterInterface::NOT_AVAILABLE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     RepairsUnrequestedTrailingPunctuationInLearnedFeedback) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("あしたはあめ", "japanese_only", "明日は雨!");

  Segments segments;
  AddSegment("あしたはあめ", "明日は飴", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_TRUE(rewriter.Rewrite(request, &segments));

  const Segment& segment = segments.conversion_segment(0);
  ASSERT_GE(segment.candidates_size(), 2);
  EXPECT_EQ(segment.candidate(0).value, "明日は飴");
  EXPECT_EQ(segment.candidate(1).value, "明日は雨");
}

TEST(ZenzFeedbackCandidateRewriterTest,
     SkipsLearnedFeedbackThatBecomesSameAsMozcAfterSymbolRepair) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("するな", "japanese_only", "するな!");

  Segments segments;
  AddSegment("するな", "するな", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "するな");
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotReintroduceMutatedTechnicalTokenFeedback) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("しーぷらすぷらす", "empty", "C#");

  Segments segments;
  AddSegment("しーぷらすぷらす", "C++", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "C++");
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
}

TEST(ZenzFeedbackCandidateRewriterTest,
     DoesNotReintroduceUnlicensedAlphabeticFeedback) {
  ScopedUserProfileForZenzFeedbackCandidateRewriterTest profile;
  ASSERT_TRUE(profile.ok());

  session::ZenzFeedbackStore store;
  store.RecordAccepted("とうきょう", "empty", "Tokyo");

  Segments segments;
  AddSegment("とうきょう", "東京", &segments);

  const ConversionRequest request = CreateZenzFeedbackConversionRequest();

  ZenzFeedbackCandidateRewriter rewriter;
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "東京");
  EXPECT_TRUE(segment.candidate(0).attributes &
              converter::Attribute::BEST_CANDIDATE);
}

#else  // defined(_WIN32)

TEST(ZenzFeedbackCandidateRewriterTest, SkippedOnNonWindows) {
  GTEST_SKIP() << "Zenz feedback candidate rewriter uses LocalLow on Windows.";
}

#endif  // defined(_WIN32)

}  // namespace
}  // namespace mozc