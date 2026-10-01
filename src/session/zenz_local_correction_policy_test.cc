#include "session/zenz_local_correction_policy.h"

#include <string>
#include <vector>

#include "session/zenz_segment_projection.h"
#include "testing/gunit.h"

namespace mozc::session {
namespace {

TEST(ZenzLocalCorrectionPolicyTest,
     ExtractsSentenceLocalCorrectionReturningToMozcBaseline) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっと", "未コミット"},
      {"だからといって", "だからといって"},
  };

  const std::vector<ZenzLocalCorrection> corrections =
      ExtractZenzLocalCorrections(
          baseline,
          "みこみっとだからといって",
          "ミコミットだからといって",
          "未コミットだからといって");

  ASSERT_EQ(corrections.size(), 1);
  EXPECT_EQ(corrections[0].reading, "みこみっと");
  EXPECT_EQ(corrections[0].rejected_value, "ミコミット");
  EXPECT_EQ(corrections[0].accepted_value, "未コミット");
}

TEST(ZenzLocalCorrectionPolicyTest,
     DoesNotExtractUserChoiceThatIsNotMozcBaseline) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっと", "未コミット"},
      {"です", "です"},
  };

  EXPECT_TRUE(
      ExtractZenzLocalCorrections(
          baseline,
          "みこみっとです",
          "ミコミットです",
          "見コミットです")
          .empty());
}

TEST(ZenzLocalCorrectionPolicyTest,
     DoesNotGuessAmbiguousMultiSegmentProjection) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"あ", "甲"},
      {"い", "乙"},
  };

  EXPECT_TRUE(
      ExtractZenzLocalCorrections(
          baseline,
          "あい",
          "丙丁",
          "甲乙")
          .empty());
}

TEST(ZenzLocalCorrectionPolicyTest,
     ExtractsMultipleIndependentKnownBoundaryCorrections) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっと", "未コミット"},
      {"なので", "なので"},
      {"ほぞん", "保存"},
  };

  const std::vector<ZenzLocalCorrection> corrections =
      ExtractZenzLocalCorrections(
          baseline,
          "みこみっとなのでほぞん",
          "ミコミットなのでホゾン",
          "未コミットなので保存");

  ASSERT_EQ(corrections.size(), 2);
  EXPECT_EQ(corrections[0].reading, "みこみっと");
  EXPECT_EQ(corrections[0].rejected_value, "ミコミット");
  EXPECT_EQ(corrections[0].accepted_value, "未コミット");
  EXPECT_EQ(corrections[1].reading, "ほぞん");
  EXPECT_EQ(corrections[1].rejected_value, "ホゾン");
  EXPECT_EQ(corrections[1].accepted_value, "保存");
}

TEST(ZenzLocalCorrectionPolicyTest,
     ReplaysExactRejectedSurfaceOnlyWhenCurrentBaselineStillMatches) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっと", "未コミット"},
      {"だからといって", "だからといって"},
  };
  const std::vector<ZenzLocalCorrection> corrections = {{
      "みこみっと", "ミコミット", "未コミット",
  }};

  const ZenzLocalCorrectionReplayResult result =
      ReplayZenzLocalCorrections(
          baseline,
          "みこみっとだからといって",
          "ミコミットだからといって",
          corrections);

  EXPECT_EQ(result.value, "未コミットだからといって");
  EXPECT_EQ(result.applied_count, 1);
}

TEST(ZenzLocalCorrectionPolicyTest,
     DoesNotReplayDifferentRejectedSurface) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっと", "未コミット"},
      {"です", "です"},
  };
  const std::vector<ZenzLocalCorrection> corrections = {{
      "みこみっと", "ミコミット", "未コミット",
  }};

  const ZenzLocalCorrectionReplayResult result =
      ReplayZenzLocalCorrections(
          baseline,
          "みこみっとです",
          "見コミットです",
          corrections);

  EXPECT_EQ(result.value, "見コミットです");
  EXPECT_EQ(result.applied_count, 0);
}

TEST(ZenzLocalCorrectionPolicyTest,
     DoesNotReplayWhenCurrentMozcBaselineChanged) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっと", "見コミット"},
      {"です", "です"},
  };
  const std::vector<ZenzLocalCorrection> corrections = {{
      "みこみっと", "ミコミット", "未コミット",
  }};

  const ZenzLocalCorrectionReplayResult result =
      ReplayZenzLocalCorrections(
          baseline,
          "みこみっとです",
          "ミコミットです",
          corrections);

  EXPECT_EQ(result.value, "ミコミットです");
  EXPECT_EQ(result.applied_count, 0);
}

TEST(ZenzLocalCorrectionPolicyTest,
     DoesNotReplayAcrossAmbiguousProjection) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"あ", "甲"},
      {"い", "乙"},
  };
  const std::vector<ZenzLocalCorrection> corrections = {{
      "あい", "丙丁", "甲乙",
  }};

  const ZenzLocalCorrectionReplayResult result =
      ReplayZenzLocalCorrections(
          baseline, "あい", "丙丁", corrections);

  EXPECT_EQ(result.value, "丙丁");
  EXPECT_EQ(result.applied_count, 0);
}

TEST(ZenzLocalCorrectionPolicyTest,
     CanonicalizesAttachedJapaneseAuxiliaryToContrastiveCore) {
  const std::optional<ZenzLocalCorrection> correction =
      CanonicalizeZenzLocalCorrectionCore(
          "みこみっとです", "ミコミットです", "未コミットです");

  ASSERT_TRUE(correction.has_value());
  EXPECT_EQ(correction->reading, "みこみっと");
  EXPECT_EQ(correction->rejected_value, "ミコミット");
  EXPECT_EQ(correction->accepted_value, "未コミット");
}

TEST(ZenzLocalCorrectionPolicyTest,
     CanonicalizesSharedPrefixAndSuffixWithoutSplittingUtf8) {
  const std::optional<ZenzLocalCorrection> correction =
      CanonicalizeZenzLocalCorrectionCore(
          "まだみこみっとでした",
          "まだミコミットでした",
          "まだ未コミットでした");

  ASSERT_TRUE(correction.has_value());
  EXPECT_EQ(correction->reading, "みこみっと");
  EXPECT_EQ(correction->rejected_value, "ミコミット");
  EXPECT_EQ(correction->accepted_value, "未コミット");
}

TEST(ZenzLocalCorrectionPolicyTest,
     ExtractionStoresCoreInsteadOfAttachedSegment) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっとです", "未コミットです"},
  };

  const std::vector<ZenzLocalCorrection> corrections =
      ExtractZenzLocalCorrections(
          baseline,
          "みこみっとです",
          "ミコミットです",
          "未コミットです");

  ASSERT_EQ(corrections.size(), 1);
  EXPECT_EQ(corrections[0].reading, "みこみっと");
  EXPECT_EQ(corrections[0].rejected_value, "ミコミット");
  EXPECT_EQ(corrections[0].accepted_value, "未コミット");
}

TEST(ZenzLocalCorrectionPolicyTest,
     ReplaysOneCoreAcrossDifferentAttachedAuxiliary) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっとでした", "未コミットでした"},
  };
  const std::vector<ZenzLocalCorrection> corrections = {{
      "みこみっと", "ミコミット", "未コミット",
  }};

  const ZenzLocalCorrectionReplayResult result =
      ReplayZenzLocalCorrections(
          baseline,
          "みこみっとでした",
          "ミコミットでした",
          corrections);

  EXPECT_EQ(result.value, "未コミットでした");
  EXPECT_EQ(result.applied_count, 1);
}

TEST(ZenzLocalCorrectionPolicyTest,
     KeepsLegacyExactSegmentCorrectionReplayable) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっとです", "未コミットです"},
  };
  const std::vector<ZenzLocalCorrection> corrections = {{
      "みこみっとです", "ミコミットです", "未コミットです",
  }};

  const ZenzLocalCorrectionReplayResult result =
      ReplayZenzLocalCorrections(
          baseline,
          "みこみっとです",
          "ミコミットです",
          corrections);

  EXPECT_EQ(result.value, "未コミットです");
  EXPECT_EQ(result.applied_count, 1);
}

TEST(ZenzLocalCorrectionPolicyTest,
     DoesNotOvergeneralizeAcrossAnotherRewriteInsideSameSegment) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"みこみっとだからといって", "未コミットだからといって"},
  };
  const std::vector<ZenzLocalCorrection> corrections = {{
      "みこみっと", "ミコミット", "未コミット",
  }};

  const ZenzLocalCorrectionReplayResult result =
      ReplayZenzLocalCorrections(
          baseline,
          "みこみっとだからといって",
          "ミコミットだからと言って",
          corrections);

  EXPECT_EQ(result.value, "ミコミットだからと言って");
  EXPECT_EQ(result.applied_count, 0);
}

}  // namespace
}  // namespace mozc::session