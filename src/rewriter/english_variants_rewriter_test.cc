// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "rewriter/english_variants_rewriter.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "composer/composer.h"
#include "composer/english_input_exceptions.h"
#include "composer/table.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/pos_matcher.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"
#include "testing/test_peer.h"
#include "transliteration/transliteration.h"

namespace mozc {

using ::mozc::dictionary::PosMatcher;

class EnglishVariantsRewriterTestPeer
    : testing::TestPeer<EnglishVariantsRewriter> {
 public:
  explicit EnglishVariantsRewriterTestPeer(EnglishVariantsRewriter& rewriter)
      : testing::TestPeer<EnglishVariantsRewriter>(rewriter) {}

  PEER_METHOD(ExpandEnglishVariants);
  PEER_METHOD(ExpandSpacePrefixedVariants);
};

class EnglishVariantsRewriterTest : public testing::TestWithTempUserProfile {
 protected:
  EnglishVariantsRewriterTest()
      : pos_matcher_(mock_data_manager_.GetPosMatcherData()) {}

  void SetUp() override {
    rewriter_ = std::make_unique<EnglishVariantsRewriter>(pos_matcher_);
  }

  bool GetRankFromValue(const Segment& segment, const absl::string_view value,
                        int* rank) {
    for (size_t i = 0; i < segment.candidates_size(); ++i) {
      if (segment.candidate(i).value == value) {
        *rank = static_cast<int>(i);
        return true;
      }
    }
    return false;
  }

  EnglishVariantsRewriterTestPeer rewriter_peer() {
    return EnglishVariantsRewriterTestPeer(*rewriter_);
  }

 private:
  const testing::MockDataManager mock_data_manager_;

 protected:
  const PosMatcher pos_matcher_;
  std::unique_ptr<EnglishVariantsRewriter> rewriter_;
};

TEST_F(EnglishVariantsRewriterTest, ExpandEnglishVariants) {
  std::vector<std::string> variants;

  EXPECT_TRUE(rewriter_peer().ExpandEnglishVariants("foo", &variants));
  EXPECT_EQ(variants.size(), 2);
  EXPECT_EQ(variants[0], "Foo");
  EXPECT_EQ(variants[1], "FOO");

  EXPECT_TRUE(rewriter_peer().ExpandEnglishVariants("Bar", &variants));
  EXPECT_EQ(variants.size(), 2);
  EXPECT_EQ(variants[0], "bar");
  EXPECT_EQ(variants[1], "BAR");

  EXPECT_TRUE(rewriter_peer().ExpandEnglishVariants("HOGE", &variants));
  EXPECT_EQ(variants.size(), 2);
  EXPECT_EQ(variants[0], "hoge");
  EXPECT_EQ(variants[1], "Hoge");

  EXPECT_FALSE(rewriter_peer().ExpandEnglishVariants("Foo Bar", &variants));

  EXPECT_TRUE(rewriter_peer().ExpandEnglishVariants("iPhone", &variants));
  EXPECT_EQ(variants.size(), 1);
  EXPECT_EQ(variants[0], "iphone");

  EXPECT_TRUE(rewriter_peer().ExpandEnglishVariants("MeCab", &variants));
  EXPECT_EQ(variants.size(), 1);
  EXPECT_EQ(variants[0], "mecab");

  EXPECT_FALSE(rewriter_peer().ExpandEnglishVariants("グーグル", &variants));
}

TEST_F(EnglishVariantsRewriterTest, ExpandSpacePrefixedVariants) {
  const EnglishVariantsRewriterTestPeer rewriter(*rewriter_);

  {
    std::vector<std::string> variants;

    EXPECT_TRUE(
        rewriter_peer().ExpandSpacePrefixedVariants("Watch", &variants));
    EXPECT_EQ(variants.size(), 1);
    EXPECT_EQ(variants[0], " Watch");

    variants.clear();
    EXPECT_FALSE(
        rewriter_peer().ExpandSpacePrefixedVariants(" Watch", &variants));
    EXPECT_EQ(variants.size(), 0);

    variants.clear();
    EXPECT_FALSE(rewriter_peer().ExpandSpacePrefixedVariants("", &variants));
    EXPECT_EQ(variants.size(), 0);
  }
  {
    std::vector<std::string> variants;
    variants.push_back("PIXEL");
    variants.push_back("pixel");

    EXPECT_TRUE(
        rewriter_peer().ExpandSpacePrefixedVariants("Pixel", &variants));
    EXPECT_EQ(variants.size(), 5);
    EXPECT_EQ(variants[0], " Pixel");
    EXPECT_EQ(variants[1], "PIXEL");
    EXPECT_EQ(variants[2], " PIXEL");
    EXPECT_EQ(variants[3], "pixel");
    EXPECT_EQ(variants[4], " pixel");
  }
}

TEST_F(EnglishVariantsRewriterTest, RewriteTest) {
  // T13N
  {
    Segments segments;
    const ConversionRequest request;
    Segment* seg = segments.push_back_segment();
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "Google";
    candidate->content_value = "Google";
    candidate->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;

    EXPECT_EQ(seg->candidates_size(), 1);
    EXPECT_EQ(seg->candidate(0).value, "Google");
    EXPECT_EQ(seg->candidate(0).content_value, "Google");
    EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
    EXPECT_EQ(seg->candidates_size(), 3);
    EXPECT_EQ(seg->candidate(0).value, "Google");
    EXPECT_EQ(seg->candidate(0).content_value, "Google");
    EXPECT_EQ(seg->candidate(1).value, "google");
    EXPECT_EQ(seg->candidate(1).content_value, "google");
    EXPECT_EQ(seg->candidate(2).value, "GOOGLE");
    EXPECT_EQ(seg->candidate(2).content_value, "GOOGLE");
  }

  // 'Google Japan'
  {
    Segments segments;
    commands::Request request;
    request.mutable_decoder_experiment_params()
        ->set_english_variation_space_insertion_mode(1);
    const ConversionRequest conversion_request =
        ConversionRequestBuilder().SetRequest(request).Build();

    Segment* seg1 = segments.push_back_segment();
    Segment* seg2 = segments.push_back_segment();
    converter::Candidate* candidate1 = seg1->add_candidate();
    candidate1->content_key = "ぐーぐる";
    candidate1->key = "ぐーぐる";
    candidate1->value = "Google";
    candidate1->content_value = "Google";
    candidate1->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
    converter::Candidate* candidate2 = seg2->add_candidate();
    candidate2->content_key = "じゃぱん";
    candidate2->key = "じゃぱん";
    candidate2->value = "Japan";
    candidate2->content_value = "Japan";
    candidate2->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
    EXPECT_EQ(seg1->candidates_size(), 1);
    EXPECT_EQ(seg1->candidate(0).value, "Google");
    EXPECT_EQ(seg1->candidate(0).content_value, "Google");
    EXPECT_EQ(seg2->candidates_size(), 1);
    EXPECT_EQ(seg2->candidate(0).value, "Japan");
    EXPECT_EQ(seg2->candidate(0).content_value, "Japan");
    EXPECT_TRUE(rewriter_->Rewrite(conversion_request, &segments));
    EXPECT_EQ(seg1->candidates_size(), 3);
    EXPECT_EQ(seg1->candidate(0).value, "Google");
    EXPECT_EQ(seg1->candidate(0).content_value, "Google");
    EXPECT_EQ(seg1->candidate(1).value, "google");
    EXPECT_EQ(seg1->candidate(1).content_value, "google");
    EXPECT_EQ(seg1->candidate(2).value, "GOOGLE");
    EXPECT_EQ(seg1->candidate(2).content_value, "GOOGLE");
    EXPECT_EQ(seg2->candidates_size(), 6);
    EXPECT_EQ(seg2->candidate(0).value, "Japan");
    EXPECT_EQ(seg2->candidate(0).content_value, "Japan");
    EXPECT_EQ(seg2->candidate(1).value, " Japan");
    EXPECT_EQ(seg2->candidate(1).content_value, " Japan");
    EXPECT_EQ(seg2->candidate(2).value, "japan");
    EXPECT_EQ(seg2->candidate(2).content_value, "japan");
    EXPECT_EQ(seg2->candidate(3).value, " japan");
    EXPECT_EQ(seg2->candidate(3).content_value, " japan");
    EXPECT_EQ(seg2->candidate(4).value, "JAPAN");
    EXPECT_EQ(seg2->candidate(4).content_value, "JAPAN");
    EXPECT_EQ(seg2->candidate(5).value, " JAPAN");
    EXPECT_EQ(seg2->candidate(5).content_value, " JAPAN");
  }

  // '<NO CANDIDATE> Japan'
  {
    Segments segments;
    commands::Request request;
    request.mutable_decoder_experiment_params()
        ->set_english_variation_space_insertion_mode(1);
    const ConversionRequest conversion_request =
        ConversionRequestBuilder().SetRequest(request).Build();

    Segment* seg1 = segments.push_back_segment();
    Segment* seg2 = segments.push_back_segment();
    // When seg1 has no candidates, seg2 will not be expanded.
    converter::Candidate* candidate2 = seg2->add_candidate();
    candidate2->content_key = "じゃぱん";

    candidate2->key = "じゃぱん";
    candidate2->value = "Japan";
    candidate2->content_value = "Japan";
    candidate2->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
    EXPECT_EQ(seg1->candidates_size(), 0);
    EXPECT_EQ(seg2->candidates_size(), 1);
    EXPECT_EQ(seg2->candidate(0).value, "Japan");
    EXPECT_EQ(seg2->candidate(0).content_value, "Japan");
    EXPECT_TRUE(rewriter_->Rewrite(conversion_request, &segments));
    EXPECT_EQ(seg1->candidates_size(), 0);
    EXPECT_EQ(seg2->candidates_size(), 3);
    EXPECT_EQ(seg2->candidate(0).value, "Japan");
    EXPECT_EQ(seg2->candidate(0).content_value, "Japan");
    EXPECT_EQ(seg2->candidate(1).value, "japan");
    EXPECT_EQ(seg2->candidate(1).content_value, "japan");
    EXPECT_EQ(seg2->candidate(2).value, "JAPAN");
    EXPECT_EQ(seg2->candidate(2).content_value, "JAPAN");
  }
  // 'ぐーぐるJapan'
  {
    Segments segments;
    commands::Request request;
    request.mutable_decoder_experiment_params()
        ->set_english_variation_space_insertion_mode(1);
    const ConversionRequest conversion_request =
        ConversionRequestBuilder().SetRequest(request).Build();

    Segment* seg1 = segments.push_back_segment();
    Segment* seg2 = segments.push_back_segment();

    // When seg1 is not an English word, seg2 will not be expanded.
    converter::Candidate* candidate1 = seg1->add_candidate();
    candidate1->content_key = "ぐーぐる";
    candidate1->key = "ぐーぐる";
    candidate1->value = "ぐーぐる";
    candidate1->content_value = "ぐーぐる";
    candidate1->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
    converter::Candidate* candidate2 = seg2->add_candidate();
    candidate2->content_key = "じゃぱん";
    candidate2->key = "じゃぱん";
    candidate2->value = "Japan";
    candidate2->content_value = "Japan";
    candidate2->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
    EXPECT_EQ(seg1->candidates_size(), 1);
    EXPECT_EQ(seg1->candidate(0).value, "ぐーぐる");
    EXPECT_EQ(seg1->candidate(0).content_value, "ぐーぐる");
    EXPECT_TRUE(rewriter_->Rewrite(conversion_request, &segments));
    EXPECT_EQ(seg2->candidates_size(), 3);
    EXPECT_EQ(seg2->candidate(0).value, "Japan");
    EXPECT_EQ(seg2->candidate(0).content_value, "Japan");
    EXPECT_EQ(seg2->candidate(1).value, "japan");
    EXPECT_EQ(seg2->candidate(1).content_value, "japan");
    EXPECT_EQ(seg2->candidate(2).value, "JAPAN");
    EXPECT_EQ(seg2->candidate(2).content_value, "JAPAN");
  }

  {
    Segments segments;
    const ConversionRequest request;
    Segment* seg = segments.push_back_segment();
    seg->Clear();

    for (int i = 0; i < 10; ++i) {
      converter::Candidate* candidate1 = seg->add_candidate();
      candidate1->value = std::to_string(i);
      candidate1->content_value = std::to_string(i);
      converter::Candidate* candidate2 = seg->add_candidate();
      candidate2->content_key = "ぐーぐる";
      candidate2->key = "ぐーぐる";
      candidate2->value = "Google";
      candidate2->content_value = "Google";
      candidate2->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
    }

    EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
    EXPECT_EQ(seg->candidates_size(), 40);

    for (int i = 0; i < 10; ++i) {
      EXPECT_EQ(seg->candidate(4 * i).value, std::to_string(i));
      EXPECT_EQ(seg->candidate(4 * i).content_value, std::to_string(i));
      EXPECT_EQ(seg->candidate(4 * i + 1).value, "Google");
      EXPECT_EQ(seg->candidate(4 * i + 1).content_value, "Google");
      EXPECT_EQ(seg->candidate(4 * i + 2).value, "google");
      EXPECT_EQ(seg->candidate(4 * i + 2).content_value, "google");
      EXPECT_EQ(seg->candidate(4 * i + 3).value, "GOOGLE");
      EXPECT_EQ(seg->candidate(4 * i + 3).content_value, "GOOGLE");
    }
  }
}

TEST_F(EnglishVariantsRewriterTest, Regression3242753) {
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  // Multi-word English candidate should not be expanded.
  // NO_VARIANTS_EXPANSION is passed to the candidate only.
  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "まいけるじゃくそん";
    candidate->key = "まいけるじゃくそん";
    candidate->value = "Michael Jackson";
    candidate->content_value = "Michael Jackson";
    candidate->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;

    EXPECT_EQ(seg->candidates_size(), 1);
    EXPECT_EQ(seg->candidate(0).value, "Michael Jackson");
    EXPECT_EQ(seg->candidate(0).content_value, "Michael Jackson");
    EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
    EXPECT_EQ(seg->candidates_size(), 1);
    EXPECT_EQ(seg->candidate(0).value, "Michael Jackson");
    EXPECT_EQ(seg->candidate(0).content_value, "Michael Jackson");
    EXPECT_NE((seg->candidate(0).attributes &
               converter::Attribute::NO_VARIANTS_EXPANSION),
              0);
  }
}

TEST_F(EnglishVariantsRewriterTest, Regression5137299) {
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "Google";
    candidate->content_value = "Google";
    candidate->attributes |= converter::Attribute::NO_VARIANTS_EXPANSION;
    EXPECT_EQ(seg->candidates_size(), 1);
    EXPECT_EQ(seg->candidate(0).value, "Google");
    EXPECT_EQ(seg->candidate(0).content_value, "Google");
    EXPECT_FALSE(rewriter_->Rewrite(request, &segments));
    EXPECT_EQ(seg->candidates_size(), 1);
  }

  {
    seg->clear_candidates();
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "Google";
    candidate->content_value = "Google";
    candidate->attributes |= converter::Attribute::NO_VARIANTS_EXPANSION;
    candidate->attributes |= converter::Attribute::USER_DICTIONARY;

    EXPECT_EQ(seg->candidates_size(), 1);
    EXPECT_EQ(seg->candidate(0).value, "Google");
    EXPECT_EQ(seg->candidate(0).content_value, "Google");
    EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
    EXPECT_EQ(seg->candidates_size(), 3);
    EXPECT_EQ(seg->candidate(0).value, "Google");
    EXPECT_EQ(seg->candidate(0).content_value, "Google");
    EXPECT_EQ(seg->candidate(1).value, "google");
    EXPECT_EQ(seg->candidate(1).content_value, "google");
    EXPECT_EQ(seg->candidate(2).value, "GOOGLE");
    EXPECT_EQ(seg->candidate(2).content_value, "GOOGLE");
  }
}

TEST_F(EnglishVariantsRewriterTest, DoNotAddDuplicatedCandidates) {
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "GOOGLE";
    candidate->content_value = "GOOGLE";

    candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "グーグル";
    candidate->content_value = "グーグル";

    candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "google";
    candidate->content_value = "google";

    EXPECT_EQ(seg->candidates_size(), 3);
    EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
    EXPECT_EQ(seg->candidates_size(), 4);  // Kana, lower, upper, capitalized
  }
}

TEST_F(EnglishVariantsRewriterTest, KeepRank) {
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "GOOGLE";
    candidate->content_value = "GOOGLE";

    candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "グーグル";
    candidate->content_value = "グーグル";

    candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->value = "google";
    candidate->content_value = "google";

    EXPECT_EQ(seg->candidates_size(), 3);
    EXPECT_TRUE(rewriter_->Rewrite(request, &segments));

    int upper_rank, lower_rank, capitalized_rank, kana_rank;
    EXPECT_TRUE(GetRankFromValue(*seg, "GOOGLE", &upper_rank));
    EXPECT_TRUE(GetRankFromValue(*seg, "google", &lower_rank));
    EXPECT_TRUE(GetRankFromValue(*seg, "Google", &capitalized_rank));
    EXPECT_TRUE(GetRankFromValue(*seg, "グーグル", &kana_rank));
    EXPECT_LT(upper_rank, lower_rank);
    EXPECT_LT(kana_rank, lower_rank);
    EXPECT_LT(lower_rank, capitalized_rank);
  }
}

TEST_F(EnglishVariantsRewriterTest, ExpandEnglishEntry) {
  // Fix variants
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "google";
    candidate->key = "google";
    candidate->value = "Google";
    candidate->content_value = "Google";
    candidate->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;

    EXPECT_EQ(seg->candidates_size(), 1);
    EXPECT_EQ(seg->candidate(0).value, "Google");
    EXPECT_EQ(seg->candidate(0).content_value, "Google");
    EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
    EXPECT_EQ(seg->candidates_size(), 1);
    EXPECT_EQ(seg->candidate(0).value, "Google");
    EXPECT_EQ(seg->candidate(0).content_value, "Google");
    EXPECT_NE((seg->candidate(0).attributes &
               converter::Attribute::NO_VARIANTS_EXPANSION),
              0);
  }
}

TEST_F(EnglishVariantsRewriterTest, DoNotExpandUpperCaseProperNouns) {
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "なさ";
    candidate->key = "なさ";
    candidate->content_value = "NASA";
    candidate->value = "NASA";
    candidate->lid = pos_matcher_.GetUniqueNounId();
    candidate->rid = pos_matcher_.GetUniqueNounId();
    candidate->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
  }

  EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
  EXPECT_EQ(seg->candidates_size(), 1);
  EXPECT_EQ(seg->candidate(0).value, "NASA");
  EXPECT_NE((seg->candidate(0).attributes &
             converter::Attribute::NO_VARIANTS_EXPANSION),
            0);
}

TEST_F(EnglishVariantsRewriterTest, ProperNouns) {
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = "ぐーぐる";
    candidate->key = "ぐーぐる";
    candidate->content_value = "google";
    candidate->value = "google";
    candidate->lid = pos_matcher_.GetUniqueNounId();
    candidate->rid = pos_matcher_.GetUniqueNounId();
    candidate->attributes &= ~converter::Attribute::NO_VARIANTS_EXPANSION;
  }

  EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
  ASSERT_EQ(seg->candidates_size(), 3);
  EXPECT_EQ(seg->candidate(0).value, "google");
  EXPECT_NE((seg->candidate(0).attributes &
             converter::Attribute::NO_VARIANTS_EXPANSION),
            0);
  EXPECT_EQ(seg->candidate(1).value, "Google");
  EXPECT_NE((seg->candidate(1).attributes &
             converter::Attribute::NO_VARIANTS_EXPANSION),
            0);
  EXPECT_EQ(seg->candidate(2).value, "GOOGLE");
  EXPECT_NE((seg->candidate(2).attributes &
             converter::Attribute::NO_VARIANTS_EXPANSION),
            0);
}

TEST_F(EnglishVariantsRewriterTest, FillConsumedKeySize) {
  Segments segments;
  const ConversionRequest request;
  Segment* seg = segments.push_back_segment();

  constexpr absl::string_view kKey = "なさ";
  {
    converter::Candidate* candidate = seg->add_candidate();
    candidate->content_key = kKey;
    candidate->key = kKey;
    candidate->content_value = "nasa";
    candidate->value = "nasa";
    candidate->consumed_key_size = kKey.size();
    candidate->attributes |= converter::Attribute::PARTIALLY_KEY_CONSUMED;
  }

  EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
  EXPECT_GT(seg->candidates_size(), 1);
  for (size_t i = 0; i < seg->candidates_size(); ++i) {
    const converter::Candidate& c = seg->candidate(i);
    EXPECT_TRUE(c.attributes & converter::Attribute::PARTIALLY_KEY_CONSUMED);
    EXPECT_EQ(c.consumed_key_size, kKey.size());
  }
}

TEST_F(EnglishVariantsRewriterTest, MobileEnvironmentTest) {
  commands::Request request;

  {
    request.set_mixed_conversion(true);
    const ConversionRequest convreq =
        ConversionRequestBuilder().SetRequest(request).Build();
    EXPECT_EQ(rewriter_->capability(convreq), RewriterInterface::ALL);
  }

  {
    request.set_mixed_conversion(false);
    const ConversionRequest convreq =
        ConversionRequestBuilder().SetRequest(request).Build();
    EXPECT_EQ(rewriter_->capability(convreq), RewriterInterface::CONVERSION);
  }
}

void AddTestEnglishException(config::Config* config,
                             absl::string_view word,
                             absl::string_view preferred,
                             config::Config::EnglishInputException::Behavior mode =
                               config::Config::EnglishInputException::AUTO_ASCII) {
  config->set_enable_english_input_exceptions(true);
  auto* rule = config->add_english_input_exceptions();
  rule->set_input(std::string(word));
  rule->set_preferred(std::string(preferred));
  rule->set_behavior(mode);
}

void AddAllTestEnglishExceptions(config::Config* config) {
  AddTestEnglishException(config, "github", "GitHub");
  AddTestEnglishException(config, "gitlab", "GitLab");
  AddTestEnglishException(config, "docker", "Docker");
  AddTestEnglishException(config, "google", "Google");
}

TEST_F(EnglishVariantsRewriterTest, CanonicalTechNamesAcrossModes) {
  for (const ConversionRequest::RequestType type : {
           ConversionRequest::CONVERSION, ConversionRequest::SUGGESTION,
           ConversionRequest::PREDICTION}) {
    for (const absl::string_view word : {"google", "github", "gitlab",
                                         "docker"}) {
      config::Config config;
      config.set_preedit_method(config::Config::ROMAN);
      config.set_auto_switch_composition_mode(true);
      config.set_shift_key_mode_switch(config::Config::OFF);
      AddAllTestEnglishExceptions(&config);
      commands::Request client_request;
      auto table = std::make_shared<composer::Table>();
      ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request,
                                                       config));
      composer::Composer composer(table, client_request, config);
      for (char c : word) {
        commands::KeyEvent key;
        key.set_key_code(c);
        ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
      }
      ASSERT_EQ(composer.GetRawString(), word);
      ASSERT_EQ(composer.GetStringForPreedit(), word);
      const ConversionRequest request = ConversionRequestBuilder()
          .SetComposer(composer)
          .SetConfig(config)
          .SetRequest(client_request)
          .SetRequestType(type)
          .Build();
      ASSERT_EQ(request.key(), word) << static_cast<int>(type);
      EXPECT_NE(rewriter_->capability(request) &
                    (type == ConversionRequest::CONVERSION
                         ? RewriterInterface::CONVERSION
                         : type == ConversionRequest::SUGGESTION
                               ? RewriterInterface::SUGGESTION
                               : RewriterInterface::PREDICTION),
                0);
      Segments segments;
      Segment* segment = segments.add_segment();
      segment->set_key(request.key());
      auto* candidate = segment->add_candidate();
      candidate->key = std::string(request.key());
      candidate->content_key = candidate->key;
      candidate->value = "仮";
      candidate->content_value = candidate->value;
      EXPECT_TRUE(rewriter_->Rewrite(request, &segments)) << word;
      const std::string canonical =
          word == "google" ? "Google" :
          word == "github" ? "GitHub" :
          word == "gitlab" ? "GitLab" : "Docker";
      std::vector<std::string> spellings = {canonical, std::string(word)};
      if (word == "github") spellings.push_back("Github");
      if (word == "gitlab") spellings.push_back("Gitlab");
      spellings.push_back(word == "google" ? "GOOGLE"
                          : word == "github" ? "GITHUB"
                          : word == "gitlab" ? "GITLAB"
                                             : "DOCKER");
      ASSERT_EQ(segment->candidates_size(), spellings.size() + 1);
      int index = 0;
      for (const std::string& spelling : spellings) {
        const converter::Candidate& actual = segment->candidate(index++);
        EXPECT_EQ(actual.value, spelling) << word;
        // Each variant is independently hideable via (reading, surface).
        EXPECT_EQ(actual.key, word) << spelling;
        EXPECT_EQ(actual.content_key, word) << spelling;
        EXPECT_EQ(actual.content_value, spelling) << word;
        EXPECT_EQ(actual.attributes & converter::Attribute::COMMAND_CANDIDATE,
                  0);
      }
      EXPECT_EQ(segment->candidate(index).value, "仮");
      EXPECT_EQ(composer.GetRawString(), word);
      // Rewrites must be idempotent (no duplicated candidates).
      rewriter_->Rewrite(request, &segments);
      EXPECT_EQ(segment->candidates_size(), spellings.size() + 1);
    }
  }
}

TEST_F(EnglishVariantsRewriterTest, CanonicalInJapaneseContext) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddAllTestEnglishExceptions(&config);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("korehagithubdesu")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_EQ(composer.GetStringForPreedit(), "これはgithubです");
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  ASSERT_EQ(request.key(), "これはgithubです");
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* candidate = segment->add_candidate();
  candidate->key = std::string(request.key());
  candidate->content_key = candidate->key;
  candidate->value = std::string(request.key());
  candidate->content_value = candidate->value;
  ASSERT_TRUE(rewriter_->Rewrite(request, &segments));
  EXPECT_EQ(segment->candidate(0).value, "これはGitHubです");
  EXPECT_EQ(segment->candidate(1).value, "これはgithubです");
  EXPECT_EQ(composer.GetRawString(), "korehagithubdesu");
}

TEST_F(EnglishVariantsRewriterTest,
       CanonicalInJapaneseContextSkipsUnalignedCandidate) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddAllTestEnglishExceptions(&config);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("korehagithubdesu")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_EQ(composer.GetStringForPreedit(), "これはgithubです");
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* candidate = segment->add_candidate();
  candidate->key = std::string(request.key());
  candidate->content_key = candidate->key;
  // Both strings contain "github", but neither has proven raw alignment.
  candidate->value = "githubこれはgithubです";
  candidate->content_value = candidate->value;
  EXPECT_FALSE(rewriter_->Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 1);
  EXPECT_EQ(segment->candidate(0).value, "githubこれはgithubです");
}

TEST_F(EnglishVariantsRewriterTest,
       CanonicalInJapaneseContextSupportsContentSuffix) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddAllTestEnglishExceptions(&config);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("korehagithubdesu")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_EQ(composer.GetStringForPreedit(), "これはgithubです");
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* candidate = segment->add_candidate();
  candidate->key = std::string(request.key());
  candidate->content_key = candidate->key;
  candidate->value = "これはgithubです";
  candidate->content_value = "githubです";  // Japanese prefix is omitted.
  ASSERT_TRUE(rewriter_->Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 2);
  EXPECT_EQ(segment->candidate(0).value, "これはGitHubです");
  EXPECT_EQ(segment->candidate(0).content_value, "GitHubです");
  EXPECT_EQ(segment->candidate(1).value, "これはgithubです");
  // Repeated rewriting must not create duplicate candidates.
  EXPECT_FALSE(rewriter_->Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 2);
}

TEST_F(EnglishVariantsRewriterTest,
       CanonicalInJapaneseContextSupportsConvertedKanaAndSuffix) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddTestEnglishException(&config, "example", "Example");
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("korehaexampledesu")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_EQ(composer.GetStringForPreedit(), "これはexampleです");
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  ASSERT_EQ(request.key(), "これはexampleです");
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* candidate = segment->add_candidate();
  candidate->key = std::string(request.key());
  candidate->content_key = candidate->key;
  candidate->value = "此れはexampleです";
  candidate->content_value = "example";
  ASSERT_TRUE(rewriter_->Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 2);
  EXPECT_EQ(segment->candidate(0).value, "此れはExampleです");
  EXPECT_EQ(segment->candidate(0).content_value, "Example");
  EXPECT_EQ(segment->candidate(0).key, request.key());
  EXPECT_EQ(segment->candidate(1).value, "此れはexampleです");
}

TEST_F(EnglishVariantsRewriterTest,
       CanonicalInJapaneseContextSupportsFollowingJapaneseSegment) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddTestEnglishException(&config, "example", "Example");
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("korehaexampledesu")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_EQ(composer.GetStringForPreedit(), "これはexampleです");
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  Segments segments;
  Segment* japanese = segments.add_segment();
  japanese->set_key("これは");
  auto* first = japanese->add_candidate();
  first->key = "これは";
  first->content_key = first->key;
  first->value = "これは";
  first->content_value = first->value;
  Segment* english = segments.add_segment();
  english->set_key("exampleです");
  auto* second = english->add_candidate();
  second->key = "exampleです";
  second->content_key = second->key;
  second->value = "exampleです";
  second->content_value = "example";
  ASSERT_TRUE(rewriter_->Rewrite(request, &segments));
  ASSERT_EQ(english->candidates_size(), 2);
  EXPECT_EQ(english->candidate(0).value, "Exampleです");
  EXPECT_EQ(english->candidate(0).content_value, "Example");
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "これは");
}

TEST_F(EnglishVariantsRewriterTest,
       CanonicalInJapaneseContextRejectsUnrelatedContent) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddTestEnglishException(&config, "example", "Example");
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("korehaexampledesu")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* candidate = segment->add_candidate();
  candidate->key = std::string(request.key());
  candidate->content_key = candidate->key;
  candidate->value = "これはexampleです";
  candidate->content_value = "example以外";  // Not contained in value.
  EXPECT_FALSE(rewriter_->Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 1);
}


TEST_F(EnglishVariantsRewriterTest,
       SuppressWholeRawRomanSuggestionInMixedJapaneseEnglish) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddTestEnglishException(&config, "example", "Example");
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("korehaexampledesu")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_EQ(composer.GetStringForPreedit(), "これはexampleです");
  ASSERT_EQ(composer.GetRawString(), "korehaexampledesu");

  for (const auto type : {ConversionRequest::SUGGESTION,
                          ConversionRequest::CONVERSION}) {
    const ConversionRequest request = ConversionRequestBuilder()
        .SetComposer(composer).SetConfig(config).SetRequest(client_request)
        .SetRequestType(type).Build();
    Segments segments;
    Segment* segment = segments.add_segment();
    segment->set_key(request.key());
    auto* raw = segment->add_candidate();
    raw->key = std::string(request.key());
    raw->content_key = raw->key;
    raw->value = "korehaexampledesu";
    raw->content_value = raw->value;
    auto* ordinary = segment->add_candidate();
    ordinary->key = std::string(request.key());
    ordinary->content_key = ordinary->key;
    ordinary->value = "これはExampleです";
    ordinary->content_value = ordinary->value;
    rewriter_->Rewrite(request, &segments);
    if (type == ConversionRequest::SUGGESTION) {
      ASSERT_EQ(segment->candidates_size(), 1);
      EXPECT_EQ(segment->candidate(0).value, "これはExampleです");
    } else {
      ASSERT_EQ(segment->candidates_size(), 2);
      EXPECT_EQ(segment->candidate(0).value, "korehaexampledesu");
    }
  }
}

TEST_F(EnglishVariantsRewriterTest,
       PendingRomanDoesNotSuggestWholeRawInputForJapaneseSentence) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_enable_english_input_exceptions(true);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));

  // No registered English token is necessary to reproduce the leak.
  // The trailing 'k' is an unresolved Roman input in Japanese mode.
  for (const std::string raw : {"korehanihongok", "korehaexampledesuk"}) {
    composer::Composer composer(table, client_request, config);
    for (char c : raw) {
      commands::KeyEvent key;
      key.set_key_code(c);
      ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
    }
    ASSERT_GT(composer.GetPendingRomanInputLength(), 0u) << raw;
    ASSERT_NE(composer.GetStringForPreedit(), raw);
    const ConversionRequest request = ConversionRequestBuilder()
        .SetComposer(composer).SetConfig(config).SetRequest(client_request)
        .SetRequestType(ConversionRequest::SUGGESTION).Build();
    EXPECT_NE(rewriter_->capability(request) & RewriterInterface::SUGGESTION,
              0) << raw;

    Segments segments;
    Segment* segment = segments.add_segment();
    segment->set_key(request.key());
    auto* raw_candidate = segment->add_candidate();
    raw_candidate->key = std::string(request.key());
    raw_candidate->content_key = raw_candidate->key;
    raw_candidate->value = raw;
    raw_candidate->content_value = raw;
    auto* japanese = segment->add_candidate();
    japanese->key = std::string(request.key());
    japanese->content_key = japanese->key;
    japanese->value = "これは日本語";
    japanese->content_value = japanese->value;
    EXPECT_TRUE(rewriter_->Rewrite(request, &segments)) << raw;
    for (const auto& candidate : segment->candidates()) {
      EXPECT_NE(candidate->value, raw) << raw;
    }
    int rank = -1;
    EXPECT_TRUE(GetRankFromValue(*segment, "これは日本語", &rank));
  }
}

TEST_F(EnglishVariantsRewriterTest,
       PendingRomanRawSuggestionRespectsDisabledFeatureAndAsciiInput) {
  for (int scenario = 0; scenario < 2; ++scenario) {
    config::Config config;
    config.set_preedit_method(config::Config::ROMAN);
    config.set_enable_english_input_exceptions(scenario != 0);
    config.set_auto_switch_composition_mode(true);
    config.set_shift_key_mode_switch(config::Config::OFF);
    commands::Request client_request;
    auto table = std::make_shared<composer::Table>();
    ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request,
                                                      config));
    composer::Composer composer(table, client_request, config);
    if (scenario == 1) {
      composer.SetInputMode(transliteration::HALF_ASCII);
    }
    const std::string raw = "korehanihongok";
    for (char c : raw) {
      commands::KeyEvent key;
      key.set_key_code(c);
      ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
    }
    const ConversionRequest request = ConversionRequestBuilder()
        .SetComposer(composer).SetConfig(config).SetRequest(client_request)
        .SetRequestType(ConversionRequest::SUGGESTION).Build();
    EXPECT_EQ(rewriter_->capability(request) & RewriterInterface::SUGGESTION,
              0) << scenario;
  }
}

TEST_F(EnglishVariantsRewriterTest,
       PendingRomanRawSuggestionSuppressionDoesNotAffectConversion) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_enable_english_input_exceptions(true);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  const std::string raw = "korehanihongok";
  for (char c : raw) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_GT(composer.GetPendingRomanInputLength(), 0u);
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* raw_candidate = segment->add_candidate();
  raw_candidate->key = std::string(request.key());
  raw_candidate->content_key = raw_candidate->key;
  raw_candidate->value = raw;
  raw_candidate->content_value = raw;
  auto* japanese = segment->add_candidate();
  japanese->key = std::string(request.key());
  japanese->content_key = japanese->key;
  japanese->value = "これは日本語";
  japanese->content_value = japanese->value;
  rewriter_->Rewrite(request, &segments);
  int rank = -1;
  EXPECT_TRUE(GetRankFromValue(*segment, raw, &rank));
}

TEST_F(EnglishVariantsRewriterTest,
       CanonicalSuggestionRequiresVisibleWordBoundary) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(true);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddAllTestEnglishExceptions(&config);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("xgithuby")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  EXPECT_EQ(composer.GetRawString(), "xgithuby");
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::SUGGESTION).Build();
  EXPECT_EQ(rewriter_->capability(request) & RewriterInterface::SUGGESTION,
            0) << "raw=" << composer.GetRawString()
               << " preedit=" << composer.GetStringForPreedit()
               << " key=" << request.key()
               << " pending=" << composer.GetPendingRomanInputLength();
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* candidate = segment->add_candidate();
  candidate->key = std::string(request.key());
  candidate->content_key = candidate->key;
  candidate->value = "仮";
  candidate->content_value = "仮";
  EXPECT_FALSE(rewriter_->Rewrite(request, &segments));
  EXPECT_EQ(segment->candidates_size(), 1);
}

TEST_F(EnglishVariantsRewriterTest, CanonicalizationIsConservative) {
  for (const absl::string_view word : {"GitHub", "GITHUB",
                                       "nihongo", "sushi", "azooKey"}) {
    config::Config config;
    config.set_preedit_method(config::Config::ROMAN);
    config.set_auto_switch_composition_mode(true);
    config.set_shift_key_mode_switch(config::Config::OFF);
    AddAllTestEnglishExceptions(&config);
    commands::Request client_request;
    auto table = std::make_shared<composer::Table>();
    ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
    composer::Composer composer(table, client_request, config);
    for (char c : word) {
      commands::KeyEvent key;
      key.set_key_code(c);
      ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
    }
    const ConversionRequest request = ConversionRequestBuilder()
        .SetComposer(composer).SetConfig(config).SetRequest(client_request)
        .SetRequestType(ConversionRequest::CONVERSION).Build();
    Segments segments;
    Segment* segment = segments.add_segment();
    segment->set_key(request.key());
    auto* candidate = segment->add_candidate();
    candidate->key = std::string(request.key());
    candidate->content_key = candidate->key;
    candidate->value = "仮";
    candidate->content_value = "仮";
    EXPECT_FALSE(rewriter_->Rewrite(request, &segments)) << word;
    EXPECT_EQ(segment->candidates_size(), 1);
  }
}

TEST_F(EnglishVariantsRewriterTest, UserDefinedExceptionsRespectSettings) {
  // Explicit opt-in. Disabled, missing entry, manual suppression and ASCII
  // input must not promote a candidate.
  for (int scenario = 0; scenario < 4; ++scenario) {
    config::Config config;
    config.set_preedit_method(config::Config::ROMAN);
    config.set_auto_switch_composition_mode(false);
    config.set_shift_key_mode_switch(config::Config::OFF);
    if (scenario != 1) AddTestEnglishException(&config, "github", "GitHub");
    if (scenario == 0) config.set_enable_english_input_exceptions(false);
    commands::Request client_request;
    auto table = std::make_shared<composer::Table>();
    ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
    composer::Composer composer(table, client_request, config);
    if (scenario == 2) composer.SuppressLanguageAwareRawInput();
    if (scenario == 3) composer.SetInputMode(transliteration::HALF_ASCII);
    for (char c : std::string("github")) {
      commands::KeyEvent key;
      key.set_key_code(c);
      ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
    }
    const ConversionRequest request = ConversionRequestBuilder()
        .SetComposer(composer).SetConfig(config).SetRequest(client_request)
        .SetRequestType(ConversionRequest::SUGGESTION).Build();
    EXPECT_EQ(rewriter_->capability(request) & RewriterInterface::SUGGESTION,
              0) << scenario;
    Segments segments;
    Segment* segment = segments.add_segment();
    segment->set_key(request.key());
    auto* candidate = segment->add_candidate();
    candidate->key = std::string(request.key());
    candidate->content_key = candidate->key;
    candidate->value = "仮";
    candidate->content_value = "仮";
    EXPECT_FALSE(rewriter_->Rewrite(request, &segments)) << scenario;
    EXPECT_EQ(segment->candidates_size(), 1) << scenario;
  }
}


// This case is deliberately *not* a single Japanese segment: a complete
// English keyboard token may look like "ちゃtgpt" in a Roman IME.
TEST_F(EnglishVariantsRewriterTest, ChatGptCandidateOnlyWorksWithSplitReading) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddTestEnglishException(&config, "chatgpt", "ChatGPT",
                          config::Config::EnglishInputException::CANDIDATE_ONLY);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char ch : std::string("chatgpt")) {
    commands::KeyEvent key;
    key.set_key_code(ch);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  ASSERT_EQ(composer.GetRawString(), "chatgpt");
  ASSERT_NE(composer.GetStringForPreedit(), "ChatGPT");
  const ConversionRequest explicit_request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  const std::string full = std::string(explicit_request.key());
  ASSERT_TRUE(full.starts_with("ちゃ")) << full;
  ASSERT_GT(full.size(), std::string("ちゃ").size());
  Segments parts;
  Segment* japanese = parts.add_segment();
  japanese->set_key("ちゃ");
  auto* original = japanese->add_candidate();
  original->key = japanese->key();
  original->content_key = original->key;
  original->value = "茶";
  original->content_value = original->value;
  Segment* suffix = parts.add_segment();
  suffix->set_key(full.substr(std::string("ちゃ").size()));
  original = suffix->add_candidate();
  original->key = suffix->key();
  original->content_key = original->key;
  original->value = suffix->key();
  original->content_value = original->value;
  ASSERT_TRUE(rewriter_->Rewrite(explicit_request, &parts));
  EXPECT_EQ(parts.conversion_segment(0).candidate(0).value, "ChatGPT");
  EXPECT_EQ(parts.conversion_segment(0).candidate(0).key, full);
  EXPECT_EQ(parts.conversion_segment(0).candidate(0)
                .effective_converted_segment_count(), 2);
  rewriter_->Rewrite(explicit_request, &parts);
  int full_span_count = 0;
  for (const auto& candidate : parts.conversion_segment(0).candidates()) {
    if (candidate->value == "ChatGPT" &&
        candidate->effective_converted_segment_count() == 2) {
      ++full_span_count;
    }
  }
  EXPECT_EQ(full_span_count, 1);

  const ConversionRequest suggest_request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::SUGGESTION)
      .SetKey(full).Build();
  Segments suggested;
  Segment* segment = suggested.add_segment();
  segment->set_key(full);
  ASSERT_TRUE(rewriter_->Rewrite(suggest_request, &suggested));
  EXPECT_EQ(segment->candidate(0).value, "ChatGPT");
}

TEST_F(EnglishVariantsRewriterTest, CandidateOnlyUsesRawKeysNotKanaOutput) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(false);
  config.set_shift_key_mode_switch(config::Config::OFF);
  AddTestEnglishException(&config, "github", "GitHub",
                          config::Config::EnglishInputException::CANDIDATE_ONLY);
  commands::Request client_request;
  auto table = std::make_shared<composer::Table>();
  ASSERT_TRUE(table->InitializeWithRequestAndConfig(client_request, config));
  composer::Composer composer(table, client_request, config);
  for (char c : std::string("github")) {
    commands::KeyEvent key;
    key.set_key_code(c);
    ASSERT_TRUE(composer.InsertCharacterKeyEvent(key));
  }
  EXPECT_NE(composer.GetStringForPreedit(), "github");
  const ConversionRequest request = ConversionRequestBuilder()
      .SetComposer(composer).SetConfig(config).SetRequest(client_request)
      .SetRequestType(ConversionRequest::CONVERSION).Build();
  Segments segments;
  Segment* segment = segments.add_segment();
  segment->set_key(request.key());
  auto* candidate = segment->add_candidate();
  candidate->key = std::string(request.key());
  candidate->content_key = candidate->key;
  candidate->value = "仮";
  candidate->content_value = "仮";
  ASSERT_TRUE(rewriter_->Rewrite(request, &segments));
  EXPECT_EQ(segment->candidate(0).value, "GitHub");
  EXPECT_EQ(segment->candidate(0).key, request.key());
}

TEST_F(EnglishVariantsRewriterTest, NoBuiltInsWhenExceptionDictionaryEmpty) {
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  config.set_auto_switch_composition_mode(false);
  config.set_enable_english_input_exceptions(true);
  EXPECT_EQ(config.english_input_exceptions_size(), 0);
  // Even with the exception engine on, no implicit canonical list exists.
  EXPECT_EQ(composer::EnglishInputExceptions::Find(config, "github"), nullptr);
}

}  // namespace mozc
