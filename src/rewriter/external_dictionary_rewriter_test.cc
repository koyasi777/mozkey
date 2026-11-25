// Copyright 2026 Mozkey contributors.
#include "rewriter/external_dictionary_rewriter.h"

#include <chrono>
#include <memory>
#include <string>

#include "base/file_util.h"
#include "base/file/temp_dir.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "dictionary/external_dictionary_manager.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

class ExternalDictionaryRewriterTest : public ::testing::Test {
 protected:
  ExternalDictionaryRewriterTest()
      : manager_(std::make_shared<dictionary::ExternalDictionaryManager>(
            std::chrono::seconds(3), false)),
        rewriter_(manager_) {}

  void Load(const std::string& tsv, bool allow_prefix = false) {
    const std::string path = FileUtil::JoinPath(dir_.path(), "external.tsv");
    ASSERT_TRUE(FileUtil::SetContents(path, tsv).ok());
    manager_->SetSources({{path, allow_prefix}});
    ASSERT_TRUE(manager_->PollOnceForTesting().ok());
  }

  Segments OneSegment(const std::string& key, const std::string& native) {
    Segments segments;
    Segment* segment = segments.push_back_segment();
    segment->set_key(key);
    converter::Candidate* c = segment->add_candidate();
    c->key = key;
    c->content_key = key;
    c->value = native;
    c->content_value = native;
    c->cost = 1000;
    return segments;
  }

  ConversionRequest Request(const std::string& key) const {
    return ConversionRequestBuilder()
        .SetRequestType(ConversionRequest::CONVERSION)
        .SetKey(key)
        .Build();
  }

  TempDirectory dir_ = testing::MakeTempDirectoryOrDie();
  std::shared_ptr<dictionary::ExternalDictionaryManager> manager_;
  ExternalDictionaryRewriter rewriter_;
};

TEST_F(ExternalDictionaryRewriterTest, ExactMatchPreservesNativeTop) {
  Load("001234\t株式会社山田商事\n001234\t山田テスト営業所\n");
  Segments segments = OneSegment("001234", "001234");
  EXPECT_TRUE(rewriter_.Rewrite(Request("001234"), &segments));
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 3);
  EXPECT_EQ(segment.candidate(0).value, "001234");
  EXPECT_EQ(segment.candidate(1).value, "株式会社山田商事");
  EXPECT_EQ(segment.candidate(2).value, "山田テスト営業所");
  EXPECT_EQ(segment.candidate(1).key, "001234");
  EXPECT_EQ(segment.candidate(1).content_key, "001234");
  EXPECT_EQ(segment.candidate(1).description, "外部辞書");
  EXPECT_TRUE(segment.candidate(1).attributes & converter::Attribute::NO_LEARNING);
  EXPECT_TRUE(segment.candidate(1).attributes & converter::Attribute::NO_MODIFICATION);
  EXPECT_TRUE(segment.candidate(1).attributes &
              converter::Attribute::NO_VARIANTS_EXPANSION);
  EXPECT_TRUE(segment.candidate(1).ranking_constraints &
              converter::RankingConstraint::NO_TOP);
}

TEST_F(ExternalDictionaryRewriterTest, RequiresExactFullKey) {
  Load("001234\t株式会社山田商事\n");
  Segments segments = OneSegment("00123", "00123");
  EXPECT_FALSE(rewriter_.Rewrite(Request("00123"), &segments));
  EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 1);
  EXPECT_FALSE(rewriter_.Rewrite(Request("001234"), &segments));
  EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 1);
}

TEST_F(ExternalDictionaryRewriterTest, DuplicateNativeSurfaceIsNotInjected) {
  Load("100\t既存の値\n100\t追加の値\n");
  Segments segments = OneSegment("100", "既存の値");
  EXPECT_TRUE(rewriter_.Rewrite(Request("100"), &segments));
  ASSERT_EQ(segments.conversion_segment(0).candidates_size(), 2);
  EXPECT_EQ(segments.conversion_segment(0).candidate(1).value, "追加の値");
}

TEST_F(ExternalDictionaryRewriterTest, RejectsPrivatePasswordAndIncognito) {
  Load("100\t顧客情報\n");
  for (int mode = 0; mode < 3; ++mode) {
    commands::Context context;
    config::Config config;
    if (mode == 0) context.set_is_private_input(true);
    if (mode == 1) context.set_input_field_type(commands::Context::PASSWORD);
    if (mode == 2) config.set_incognito_mode(true);
    const ConversionRequest request = ConversionRequestBuilder()
        .SetContext(context)
        .SetConfig(config)
        .SetRequestType(ConversionRequest::CONVERSION)
        .SetKey("100")
        .Build();
    Segments segments = OneSegment("100", "100");
    EXPECT_FALSE(rewriter_.Rewrite(request, &segments));
    EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 1);
  }
}

TEST_F(ExternalDictionaryRewriterTest, RejectsSuggestionAndPrediction) {
  Load("100\t顧客情報\n");
  for (auto type : {ConversionRequest::SUGGESTION,
                    ConversionRequest::PREDICTION}) {
    const ConversionRequest request = ConversionRequestBuilder()
        .SetRequestType(type)
        .SetKey("100")
        .Build();
    Segments segments = OneSegment("100", "100");
    EXPECT_EQ(rewriter_.capability(request), RewriterInterface::NOT_AVAILABLE);
    EXPECT_FALSE(rewriter_.Rewrite(request, &segments));
  }
}

TEST_F(ExternalDictionaryRewriterTest,
       FullReadingAcrossNativeSegmentsIsSelectableWithoutResizing) {
  Load("ぐうぐる\t外部語彙候補\n");
  Segments segments = OneSegment("ぐう", "具");
  Segment* tail = segments.push_back_segment();
  tail->set_key("ぐる");
  converter::Candidate* tail_native = tail->add_candidate();
  tail_native->key = "ぐる";
  tail_native->content_key = "ぐる";
  tail_native->value = "来る";
  tail_native->content_value = "来る";

  ASSERT_FALSE(segments.resized());
  ASSERT_TRUE(rewriter_.Rewrite(Request("ぐうぐる"), &segments));
  ASSERT_EQ(segments.conversion_segments_size(), 2);
  EXPECT_FALSE(segments.resized());
  const Segment& first = segments.conversion_segment(0);
  ASSERT_EQ(first.candidates_size(), 2);
  EXPECT_EQ(first.key(), "ぐう");
  EXPECT_EQ(first.candidate(0).value, "具");
  EXPECT_EQ(first.candidate(0).effective_converted_segment_count(), 1u);
  EXPECT_EQ(first.candidate(1).key, "ぐうぐる");
  EXPECT_EQ(first.candidate(1).value, "外部語彙候補");
  EXPECT_EQ(first.candidate(1).effective_converted_segment_count(), 2u);
  EXPECT_TRUE(first.candidate(1).attributes & converter::Attribute::NO_LEARNING);
  EXPECT_TRUE(first.candidate(1).ranking_constraints &
              converter::RankingConstraint::NO_TOP);
  EXPECT_EQ(segments.conversion_segment(1).key(), "ぐる");
  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "来る");
}

TEST_F(ExternalDictionaryRewriterTest,
       MultiSegmentRequiresFullReadingAndNativeCandidates) {
  Load("ぐうぐる\t外部語彙候補\n");
  Segments segments = OneSegment("ぐう", "具");
  Segment* tail = segments.push_back_segment();
  tail->set_key("ぐる");
  auto* candidate = tail->add_candidate();
  candidate->key = "ぐる";
  candidate->value = "来る";
  EXPECT_FALSE(rewriter_.Rewrite(Request("ぐう"), &segments));
  EXPECT_FALSE(rewriter_.Rewrite(Request("ぐうぐるに"), &segments));
  EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 1u);
  // An empty native segment is never repaired by external TSV injection.
  tail->erase_candidates(0, 1);
  EXPECT_FALSE(rewriter_.Rewrite(Request("ぐうぐる"), &segments));
}

TEST_F(ExternalDictionaryRewriterTest,
       MultiSegmentNativeFullSentenceSurfaceIsNotDuplicated) {
  Load("ぐうぐる\t具来る\n");
  Segments segments = OneSegment("ぐう", "具");
  Segment* tail = segments.push_back_segment();
  tail->set_key("ぐる");
  auto* candidate = tail->add_candidate();
  candidate->key = "ぐる";
  candidate->value = "来る";
  EXPECT_FALSE(rewriter_.Rewrite(Request("ぐうぐる"), &segments));
  EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 1u);
}

TEST_F(ExternalDictionaryRewriterTest, ObservesReplacementSnapshot) {
  Load("100\t旧会社\n");
  Segments first = OneSegment("100", "100");
  ASSERT_TRUE(rewriter_.Rewrite(Request("100"), &first));
  EXPECT_EQ(first.conversion_segment(0).candidate(1).value, "旧会社");
  const std::string path = FileUtil::JoinPath(dir_.path(), "external.tsv");
  const std::string next = FileUtil::JoinPath(dir_.path(), "next.tsv");
  ASSERT_TRUE(FileUtil::SetContents(next, "100\t新会社\n").ok());
  ASSERT_TRUE(FileUtil::AtomicRename(next, path).ok());
  ASSERT_TRUE(manager_->PollOnceForTesting().ok());
  Segments second = OneSegment("100", "100");
  ASSERT_TRUE(rewriter_.Rewrite(Request("100"), &second));
  EXPECT_EQ(second.conversion_segment(0).candidate(1).value, "新会社");
  EXPECT_EQ(first.conversion_segment(0).candidate(1).value, "旧会社");
}

TEST_F(ExternalDictionaryRewriterTest, DisabledSourceYieldsNoCandidate) {
  Load("100\t顧客情報\n");
  manager_->SetPath("");
  Segments segments = OneSegment("100", "100");
  EXPECT_FALSE(rewriter_.Rewrite(Request("100"), &segments));
}


TEST_F(ExternalDictionaryRewriterTest, PrefixFollowsExactAndUsesTypedKey) {
  Load("せいひん\t完全一致候補\n"
       "せいひんえー\t前方一致候補\n"
       "せいひんびー\t完全一致候補\n"
       "せいひんしー\t製品\n", true);
  Segments segments = OneSegment("せいひん", "製品");
  ASSERT_TRUE(rewriter_.Rewrite(Request("せいひん"), &segments));
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 3u);
  EXPECT_EQ(segment.candidate(0).value, "製品");
  EXPECT_EQ(segment.candidate(1).value, "完全一致候補");
  EXPECT_EQ(segment.candidate(1).description, "外部辞書");
  const auto& candidate = segment.candidate(2);
  EXPECT_EQ(candidate.value, "前方一致候補");
  EXPECT_EQ(candidate.description, "外部辞書（前方一致）");
  EXPECT_EQ(candidate.key, "せいひん");
  EXPECT_EQ(candidate.content_key, "せいひん");
  EXPECT_EQ(candidate.effective_converted_segment_count(), 1u);
  EXPECT_TRUE(candidate.attributes & converter::Attribute::NO_LEARNING);
  EXPECT_TRUE(candidate.attributes & converter::Attribute::NO_VARIANTS_EXPANSION);
  EXPECT_TRUE(candidate.attributes & converter::Attribute::NO_MODIFICATION);
  EXPECT_TRUE(candidate.ranking_constraints & converter::RankingConstraint::NO_TOP);
}

TEST_F(ExternalDictionaryRewriterTest, PrefixSpansOnlyTypedNativeSegments) {
  Load("ぐうぐるかぶしき\t前方一致複数文節候補\n", true);
  Segments segments = OneSegment("ぐう", "具");
  Segment* tail = segments.push_back_segment();
  tail->set_key("ぐる");
  auto* native = tail->add_candidate();
  native->key = "ぐる";
  native->value = "来る";
  EXPECT_FALSE(rewriter_.Rewrite(Request("ぐうぐるに"), &segments));
  ASSERT_TRUE(rewriter_.Rewrite(Request("ぐうぐる"), &segments));
  ASSERT_EQ(segments.conversion_segments_size(), 2u);
  EXPECT_FALSE(segments.resized());
  const Segment& first = segments.conversion_segment(0);
  ASSERT_EQ(first.candidates_size(), 2u);
  EXPECT_EQ(first.candidate(0).value, "具");
  EXPECT_EQ(first.candidate(1).value, "前方一致複数文節候補");
  EXPECT_EQ(first.candidate(1).key, "ぐうぐる");
  EXPECT_EQ(first.candidate(1).content_key, "ぐうぐる");
  EXPECT_EQ(first.candidate(1).effective_converted_segment_count(), 2u);
  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "来る");
}

TEST_F(ExternalDictionaryRewriterTest, PrefixHonorsPrivateAndRequestGuards) {
  Load("せいひんえー\t前方一致非公開候補\n", true);
  for (int mode = 0; mode < 5; ++mode) {
    commands::Context context;
    config::Config config;
    if (mode == 0) context.set_is_private_input(true);
    if (mode == 1) context.set_input_field_type(commands::Context::PASSWORD);
    if (mode == 2) config.set_incognito_mode(true);
    const auto type = mode == 3 ? ConversionRequest::SUGGESTION
                      : mode == 4 ? ConversionRequest::PREDICTION
                                  : ConversionRequest::CONVERSION;
    const auto request = ConversionRequestBuilder()
        .SetContext(context)
        .SetConfig(config)
        .SetRequestType(type)
        .SetKey("せいひん")
        .Build();
    Segments segments = OneSegment("せいひん", "製品");
    EXPECT_FALSE(rewriter_.Rewrite(request, &segments));
    EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 1u);
  }
}

}  // namespace
}  // namespace mozc
