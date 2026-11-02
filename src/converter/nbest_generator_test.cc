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

#include "converter/nbest_generator.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "converter/caching_connector.h"
#include "converter/candidate.h"
#include "converter/immutable_converter.h"
#include "converter/lattice.h"
#include "converter/node.h"
#include "converter/segments.h"
#include "converter/segments_matchers.h"
#include "data_manager/testing/mock_data_manager.h"
#include "engine/modules.h"
#include "request/conversion_request.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/test_peer.h"

namespace mozc {

using ::mozc::converter::Candidate;

class NBestGeneratorTestPeer
    : public testing::TestPeer<NBestGenerator<CachingConnector<true>>> {
 public:
  explicit NBestGeneratorTestPeer(NBestGenerator<CachingConnector<true>>& g)
      : testing::TestPeer<NBestGenerator<CachingConnector<true>>>(g) {}
  PEER_METHOD(MakeCandidate);
};

class ImmutableConverterTestPeer : testing::TestPeer<ImmutableConverter> {
 public:
  explicit ImmutableConverterTestPeer(ImmutableConverter& converter)
      : testing::TestPeer<ImmutableConverter>(converter) {}

  // Make them public via peer class.
  PEER_METHOD(IsSegmentEndNode);
  PEER_METHOD(MakeLattice);
  PEER_METHOD(Viterbi);
  PEER_METHOD(MakeGroup);
};

namespace {

class MockDataAndImmutableConverter {
 public:
  // Initializes data and immutable converter with given dictionaries.
  MockDataAndImmutableConverter() {
    modules_ =
        engine::Modules::Create(std::make_unique<testing::MockDataManager>())
            .value();
    immutable_converter_ = std::make_unique<ImmutableConverter>(*modules_);
  }

  ImmutableConverter* GetConverter() { return immutable_converter_.get(); }
  const dictionary::PosMatcher& GetPosMatcher() const {
    return modules_->GetPosMatcher();
  }
  ImmutableConverterTestPeer GetConverterTestPeer() {
    return ImmutableConverterTestPeer(*immutable_converter_);
  }

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> CreateNBestGenerator(
      const Lattice& lattice, const ConversionRequest& request) {
    caching_connector_ = std::make_unique<CachingConnector<true>>(
        modules_->GetConnector(),
        request.request()
            .decoder_experiment_params()
            .particle_omission_transition_cost_bonus(),
        modules_->GetPosMatcher());
    return std::make_unique<NBestGenerator<CachingConnector<true>>>(
        modules_->GetUserDictionary(), modules_->GetSegmenter(),
        *caching_connector_, modules_->GetPosMatcher(), lattice,
        modules_->GetSuggestionFilter());
  }

 private:
  std::unique_ptr<const engine::Modules> modules_;
  std::unique_ptr<ImmutableConverter> immutable_converter_;
  std::unique_ptr<CachingConnector<true>> caching_connector_;
};

ConversionRequest ConvReq(ConversionRequest::RequestType request_type) {
  return ConversionRequestBuilder().SetRequestType(request_type).Build();
}

}  // namespace

class NBestGeneratorTest : public ::testing::Test {
 protected:
  const Node* GetEndNode(const ConversionRequest& request,
                         ImmutableConverterTestPeer& converter,
                         const Segments& segments, const Node& begin_node,
                         absl::Span<const uint16_t> group,
                         bool is_single_segment) {
    const Node* end_node = nullptr;
    for (const Node* node = begin_node.next; node->next != nullptr;
         node = node->next) {
      end_node = node->next;
      if (converter.IsSegmentEndNode(request.options(), segments, node, group,
                                     is_single_segment)) {
        break;
      }
    }
    return end_node;
  }
};

TEST_F(NBestGeneratorTest, MultiSegmentConnectionTest) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  Segments segments;
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::FIXED_BOUNDARY);
    segment->set_key("しんこう");

    segment = segments.add_segment();
    segment->set_segment_type(Segment::FREE);
    segment->set_key("する");
  }

  Lattice lattice;
  lattice.SetKey("しんこうする");
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  converter.MakeLattice(request.options(), &segments, &lattice);

  const std::vector<uint16_t> group = converter.MakeGroup(segments);
  converter.Viterbi(request.options(), segments, &lattice);

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
      data_and_converter->CreateNBestGenerator(lattice, request);

  constexpr bool kSingleSegment = false;  // For 'normal' conversion
  const Node* begin_node = lattice.bos_node();
  const Node* end_node = GetEndNode(request, converter, segments, *begin_node,
                                    group, kSingleSegment);
  {
    nbest_generator->Reset(
        *begin_node, *end_node,
        {NBestGenerator<CachingConnector<true>>::STRICT,
         NBestGenerator<CachingConnector<true>>::CANDIDATE_MODE_NONE});
    Segment result_segment;
    nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
    // The top result is treated exceptionally and has no boundary check
    // in NBestGenerator.
    // The best route is calculated in ImmutalbeConverter with boundary check.
    // So, the top result should be inserted, but other candidates will be cut
    // due to boundary check between "する".
    ASSERT_EQ(result_segment.candidates_size(), 1);
    EXPECT_EQ(result_segment.candidate(0).value, "進行");
  }

  {
    nbest_generator->Reset(
        *begin_node, *end_node,
        {NBestGenerator<CachingConnector<true>>::ONLY_MID,
         NBestGenerator<CachingConnector<true>>::CANDIDATE_MODE_NONE});
    Segment result_segment;
    nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
    ASSERT_EQ(result_segment.candidates_size(), 3);
    EXPECT_EQ(result_segment.candidate(0).value, "進行");
    EXPECT_EQ(result_segment.candidate(1).value, "信仰");
    EXPECT_EQ(result_segment.candidate(2).value, "深耕");
  }
}

TEST_F(NBestGeneratorTest, SingleSegmentConnectionTest) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  Segments segments;
  std::string kText = "わたしのなまえはなかのです";
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::FREE);
    segment->set_key(kText);
  }

  Lattice lattice;
  lattice.SetKey(kText);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  converter.MakeLattice(request.options(), &segments, &lattice);

  const std::vector<uint16_t> group = converter.MakeGroup(segments);
  converter.Viterbi(request.options(), segments, &lattice);

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
      data_and_converter->CreateNBestGenerator(lattice, request);

  constexpr bool kSingleSegment = true;  // For real time conversion
  const Node* begin_node = lattice.bos_node();
  const Node* end_node = GetEndNode(request, converter, segments, *begin_node,
                                    group, kSingleSegment);
  {
    nbest_generator->Reset(
        *begin_node, *end_node,
        {NBestGenerator<CachingConnector<true>>::STRICT,
         NBestGenerator<CachingConnector<true>>::CANDIDATE_MODE_NONE});
    Segment result_segment;
    nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
    // Top result should be inserted, but other candidates will be cut
    // due to boundary check.
    ASSERT_EQ(result_segment.candidates_size(), 1);
    EXPECT_EQ(result_segment.candidate(0).value, "私の名前は中ノです");
  }
  {
    nbest_generator->Reset(
        *begin_node, *end_node,
        {NBestGenerator<CachingConnector<true>>::ONLY_EDGE,
         NBestGenerator<CachingConnector<true>>::FILL_INNER_SEGMENT_INFO});
    Segment result_segment;
    nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
    // We can get several candidates.
    ASSERT_LT(1, result_segment.candidates_size());
    EXPECT_EQ(result_segment.candidate(0).value, "私の名前は中ノです");
  }
}

TEST_F(NBestGeneratorTest, InnerSegmentBoundary) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  Segments segments;
  const std::string kInput = "とうきょうかなごやにいきたい";
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::FREE);
    segment->set_key(kInput);
  }

  Lattice lattice;
  lattice.SetKey(kInput);
  const ConversionRequest request = ConvReq(ConversionRequest::PREDICTION);
  converter.MakeLattice(request.options(), &segments, &lattice);

  const std::vector<uint16_t> group = converter.MakeGroup(segments);
  converter.Viterbi(request.options(), segments, &lattice);

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
      data_and_converter->CreateNBestGenerator(lattice, request);

  constexpr bool kSingleSegment = true;  // For real time conversion
  const Node* begin_node = lattice.bos_node();
  const Node* end_node = GetEndNode(request, converter, segments, *begin_node,
                                    group, kSingleSegment);

  nbest_generator->Reset(
      *begin_node, *end_node,
      {NBestGenerator<CachingConnector<true>>::ONLY_EDGE,
       NBestGenerator<CachingConnector<true>>::FILL_INNER_SEGMENT_INFO});
  Segment result_segment;
  nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
  ASSERT_LE(1, result_segment.candidates_size());

  const Candidate& top_cand = result_segment.candidate(0);
  EXPECT_EQ(top_cand.key, kInput);
  EXPECT_EQ(top_cand.value, "東京か名古屋に行きたい");

  std::vector<absl::string_view> keys, values, content_keys, content_values;
  for (const auto& iter : top_cand.inner_segments()) {
    keys.push_back(iter.GetKey());
    values.push_back(iter.GetValue());
    content_keys.push_back(iter.GetContentKey());
    content_values.push_back(iter.GetContentValue());
  }
  ASSERT_EQ(keys.size(), 3);
  ASSERT_EQ(values.size(), 3);
  ASSERT_EQ(content_keys.size(), 3);
  ASSERT_EQ(content_values.size(), 3);

  // Inner segment 0
  EXPECT_EQ(keys[0], "とうきょうか");
  EXPECT_EQ(values[0], "東京か");
  EXPECT_EQ(content_keys[0], "とうきょう");
  EXPECT_EQ(content_values[0], "東京");

  // Inner segment 1
  EXPECT_EQ(keys[1], "なごやに");
  EXPECT_EQ(values[1], "名古屋に");
  EXPECT_EQ(content_keys[1], "なごや");
  EXPECT_EQ(content_values[1], "名古屋");

  // Inner segment 2: In the original segment, "行きたい" has the form
  // "行き" (content word) + "たい" (functional).  However, since "行き" is
  // Yougen, our rule for inner segment boundary doesn't handle it as a content
  // value.  Thus, "行きたい" becomes the content value.
  EXPECT_EQ(keys[2], "いきたい");
  EXPECT_EQ(values[2], "行きたい");
  EXPECT_EQ(content_keys[2], "いきたい");
  EXPECT_EQ(content_values[2], "行きたい");

  // The selected dictionary node path must expose the proper nouns embedded
  // in a longer, single-candidate conversion, not only the first/last POS.
  // Verify UTF-8 byte offsets against the actual candidate strings.
  bool saw_tokyo = false;
  bool saw_nagoya = false;
  for (const converter::ProperNounSpan& span : top_cand.proper_noun_spans) {
    ASSERT_LE(span.key_offset, top_cand.key.size());
    ASSERT_LE(span.value_offset, top_cand.value.size());
    EXPECT_EQ(top_cand.key.substr(span.key_offset, span.key.size()), span.key);
    EXPECT_EQ(top_cand.value.substr(span.value_offset, span.value.size()),
              span.value);
    if (span.key == "とうきょう" && span.value == "東京") {
      EXPECT_EQ(span.key_offset, 0);
      EXPECT_EQ(span.value_offset, 0);
      saw_tokyo = true;
    }
    if (span.key == "なごや" && span.value == "名古屋") {
      saw_nagoya = true;
    }
  }
  EXPECT_TRUE(saw_tokyo);
  EXPECT_TRUE(saw_nagoya);
}

TEST_F(NBestGeneratorTest, NoPartialCandidateBetweenAlphabets) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  Segments segments;
  const std::string kInput = "AAA";
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::FREE);
    segment->set_key(kInput);
  }

  Lattice lattice;
  lattice.SetKey(kInput);
  const ConversionRequest request = ConvReq(ConversionRequest::PREDICTION);
  converter.MakeLattice(request.options(), &segments, &lattice);

  const std::vector<uint16_t> group = converter.MakeGroup(segments);
  converter.Viterbi(request.options(), segments, &lattice);

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
      data_and_converter->CreateNBestGenerator(lattice, request);

  constexpr bool kSingleSegment = true;  // For real time conversion
  const Node* begin_node = lattice.bos_node();
  const Node* end_node = GetEndNode(request, converter, segments, *begin_node,
                                    group, kSingleSegment);

  // Since the test dictionary contains "A", partial candidates "A" and "AA" can
  // be generated but they should be suppressed because they are split between
  // alphabets.
  const NBestGenerator<CachingConnector<true>>::Options options = {
      .boundary_mode = NBestGenerator<CachingConnector<true>>::ONLY_EDGE,
      .candidate_mode =
          NBestGenerator<
              CachingConnector<true>>::BUILD_FROM_ONLY_FIRST_INNER_SEGMENT |
          NBestGenerator<CachingConnector<true>>::FILL_INNER_SEGMENT_INFO,
  };
  nbest_generator->Reset(*begin_node, *end_node, options);
  Segment result_segment;
  nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
  EXPECT_THAT(result_segment, HasSingleCandidate(::testing::Field(
                                  "value", &Candidate::value, "AAA")));
}

TEST_F(NBestGeneratorTest, NoAlphabetsConnection2Nodes) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  Segments segments;
  std::string kText = "eupho";
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::FREE);
    segment->set_key(kText);
  }

  Lattice lattice;
  lattice.SetKey(kText);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  converter.MakeLattice(request.options(), &segments, &lattice);

  const std::vector<uint16_t> group = converter.MakeGroup(segments);
  converter.Viterbi(request.options(), segments, &lattice);

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
      data_and_converter->CreateNBestGenerator(lattice, request);

  constexpr bool kSingleSegment = true;  // For real time conversion
  const Node* begin_node = lattice.bos_node();
  const Node* end_node = GetEndNode(request, converter, segments, *begin_node,
                                    group, kSingleSegment);
  nbest_generator->Reset(
      *begin_node, *end_node,
      {NBestGenerator<CachingConnector<true>>::ONLY_EDGE,
       NBestGenerator<CachingConnector<true>>::FILL_INNER_SEGMENT_INFO});
  Segment result_segment;
  nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
  // The test dictionary contains key value pairs (eu, EU) and (pho, pho), but
  // "EUpho" should not be generated as it is a concatenation of two alphabet
  // words. The only expected candidate is (eupho, eupho).
  EXPECT_THAT(result_segment, HasSingleCandidate(::testing::Field(
                                  "value", &Candidate::value, "eupho")));
}

TEST_F(NBestGeneratorTest, NoAlphabetsConnection3Nodes) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  Segments segments;
  std::string kText = "euphoとうきょう";
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::FREE);
    segment->set_key(kText);
  }

  Lattice lattice;
  lattice.SetKey(kText);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  converter.MakeLattice(request.options(), &segments, &lattice);

  const std::vector<uint16_t> group = converter.MakeGroup(segments);
  converter.Viterbi(request.options(), segments, &lattice);

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
      data_and_converter->CreateNBestGenerator(lattice, request);

  constexpr bool kSingleSegment = true;  // For real time conversion
  const Node* begin_node = lattice.bos_node();
  const Node* end_node = GetEndNode(request, converter, segments, *begin_node,
                                    group, kSingleSegment);
  nbest_generator->Reset(
      *begin_node, *end_node,
      {NBestGenerator<CachingConnector<true>>::ONLY_EDGE,
       NBestGenerator<CachingConnector<true>>::FILL_INNER_SEGMENT_INFO});
  Segment result_segment;
  nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
  // Tne top candidate consists of two elements, "eupho" and "東京". Such
  // connection from English word to a normal word is possible.
  ASSERT_GE(result_segment.candidates_size(), 1);
  EXPECT_EQ(result_segment.candidate(0).value, "eupho東京");
  EXPECT_EQ(result_segment.candidate(0).inner_segment_boundary.size(), 2);
  // However, we should not concatenate "EU", "pho", and "東京".
  for (size_t i = 0; i < result_segment.candidates_size(); ++i) {
    EXPECT_NE(result_segment.candidate(i).value, "EUpho東京");
  }
}

TEST_F(NBestGeneratorTest, AlphabetsConnectionAcrossSegmentBoundary) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  Segments segments;
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::HISTORY);
    segment->set_key("eu");
    Candidate* candidate = segment->add_candidate();
    candidate->key = "eu";
    candidate->value = "EU";
  }
  {
    Segment* segment = segments.add_segment();
    segment->set_segment_type(Segment::FREE);
    segment->set_key("pho");
  }

  Lattice lattice;
  lattice.SetKey("pho");
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  converter.MakeLattice(request.options(), &segments, &lattice);

  const std::vector<uint16_t> group = converter.MakeGroup(segments);
  converter.Viterbi(request.options(), segments, &lattice);

  std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
      data_and_converter->CreateNBestGenerator(lattice, request);

  // Skip HIS_NODE(s) to find the begin_node for the conversion segment.
  const Node* begin_node = lattice.bos_node();
  for (const Node* node = lattice.bos_node()->next;
       node->next != nullptr && node->node_type == Node::HIS_NODE;
       node = node->next) {
    begin_node = node;
  }

  constexpr bool kSingleSegment = false;
  const Node* end_node = GetEndNode(request, converter, segments, *begin_node,
                                    group, kSingleSegment);
  nbest_generator->Reset(
      *begin_node, *end_node,
      {NBestGenerator<CachingConnector<true>>::STRICT,
       NBestGenerator<CachingConnector<true>>::CANDIDATE_MODE_NONE});
  Segment result_segment;
  nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
  // Connection between "eu" and "pho" across the segment boundary should be
  // allowed even though both keys end/start with alphabets.
  ASSERT_GE(result_segment.candidates_size(), 1);
  EXPECT_EQ(result_segment.candidate(0).value, "pho");
}

TEST_F(NBestGeneratorTest, ParticleOmissionBonusTest) {
  auto data_and_converter = std::make_unique<MockDataAndImmutableConverter>();
  ImmutableConverterTestPeer converter =
      data_and_converter->GetConverterTestPeer();

  // Case 1: Noun ("あんけーと") + Verb ("ひかえる")
  {
    Segments segments;
    {
      Segment* segment = segments.add_segment();
      segment->set_segment_type(Segment::FREE);
      segment->set_key("あんけーと");
      segment = segments.add_segment();
      segment->set_segment_type(Segment::FREE);
      segment->set_key("ひかえる");
    }

    Lattice lattice_without_bonus;
    lattice_without_bonus.SetKey("あんけーとひかえる");

    int cost_without_bonus = 0;
    {
      const ConversionRequest request =
          ConversionRequestBuilder()
              .SetRequestType(ConversionRequest::CONVERSION)
              .Build();
      converter.MakeLattice(request.options(), &segments,
                            &lattice_without_bonus);
      const std::vector<uint16_t> group = converter.MakeGroup(segments);
      converter.Viterbi(request.options(), segments, &lattice_without_bonus);

      std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
          data_and_converter->CreateNBestGenerator(lattice_without_bonus,
                                                   request);
      const Node* begin_node = lattice_without_bonus.bos_node();
      const Node* end_node =
          GetEndNode(request, converter, segments, *begin_node, group, false);
      nbest_generator->Reset(
          *begin_node, *end_node,
          {NBestGenerator<CachingConnector<true>>::STRICT,
           NBestGenerator<CachingConnector<true>>::CANDIDATE_MODE_NONE});
      Segment result_segment;
      nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
      ASSERT_GE(result_segment.candidates_size(), 1);
      EXPECT_EQ(result_segment.candidate(0).value, "アンケート");
      cost_without_bonus = result_segment.candidate(0).cost;
    }

    int cost_bonus_2000 = 0;
    {
      commands::Request request_proto;
      request_proto.mutable_decoder_experiment_params()
          ->set_particle_omission_transition_cost_bonus(2000);
      const ConversionRequest request =
          ConversionRequestBuilder()
              .SetRequest(request_proto)
              .SetRequestType(ConversionRequest::CONVERSION)
              .Build();

      Lattice lattice_with_bonus;
      lattice_with_bonus.SetKey("あんけーとひかえる");
      converter.MakeLattice(request.options(), &segments, &lattice_with_bonus);
      const std::vector<uint16_t> group = converter.MakeGroup(segments);
      converter.Viterbi(request.options(), segments, &lattice_with_bonus);

      std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
          data_and_converter->CreateNBestGenerator(lattice_with_bonus, request);
      const Node* begin_node = lattice_with_bonus.bos_node();
      const Node* end_node =
          GetEndNode(request, converter, segments, *begin_node, group, false);
      nbest_generator->Reset(
          *begin_node, *end_node,
          {NBestGenerator<CachingConnector<true>>::STRICT,
           NBestGenerator<CachingConnector<true>>::CANDIDATE_MODE_NONE});
      Segment result_segment;
      nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
      ASSERT_GE(result_segment.candidates_size(), 1);
      EXPECT_EQ(result_segment.candidate(0).value, "アンケート");
      cost_bonus_2000 = result_segment.candidate(0).cost;
    }

    int cost_bonus_2001 = 0;
    {
      commands::Request request_proto;
      request_proto.mutable_decoder_experiment_params()
          ->set_particle_omission_transition_cost_bonus(2001);
      const ConversionRequest request =
          ConversionRequestBuilder()
              .SetRequest(request_proto)
              .SetRequestType(ConversionRequest::CONVERSION)
              .Build();

      Lattice lattice_with_bonus;
      lattice_with_bonus.SetKey("あんけーとひかえる");
      converter.MakeLattice(request.options(), &segments, &lattice_with_bonus);
      const std::vector<uint16_t> group = converter.MakeGroup(segments);
      converter.Viterbi(request.options(), segments, &lattice_with_bonus);

      std::unique_ptr<NBestGenerator<CachingConnector<true>>> nbest_generator =
          data_and_converter->CreateNBestGenerator(lattice_with_bonus, request);
      const Node* begin_node = lattice_with_bonus.bos_node();
      const Node* end_node =
          GetEndNode(request, converter, segments, *begin_node, group, false);
      nbest_generator->Reset(
          *begin_node, *end_node,
          {NBestGenerator<CachingConnector<true>>::STRICT,
           NBestGenerator<CachingConnector<true>>::CANDIDATE_MODE_NONE});
      Segment result_segment;
      nbest_generator->SetCandidates(request.options(), "", 10, result_segment);
      ASSERT_GE(result_segment.candidates_size(), 1);
      EXPECT_EQ(result_segment.candidate(0).value, "アンケート");
      cost_bonus_2001 = result_segment.candidate(0).cost;
    }

    // Cost should be reduced by 1 for bonus 2001 compared to 2000.
    EXPECT_EQ(cost_bonus_2000 - cost_bonus_2001, 1);
  }
}


// These tests use synthetic lattice nodes.  They neither require daily-profile
// data nor rely on whichever candidate happened to rank first.
TEST_F(NBestGeneratorTest, GeneralNounFullNameRecoveredFromLattice) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  Lattice lattice;
  lattice.SetKey("みかみあい");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = fixture->GetPosMatcher().GetGeneralNounId();  // general noun
  lattice.Insert(0, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = fixture->GetPosMatcher().GetLastNameId();  // surname
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = fixture->GetPosMatcher().GetFirstNameId();  // given name
  lattice.Insert(std::string("みかみ").size(), given);

  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  auto generator = fixture->CreateNBestGenerator(lattice, request);
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  const auto& span = candidate.proper_noun_spans[0];
  EXPECT_EQ(span.key, "みかみあい");
  EXPECT_EQ(span.value, "見上愛");
  EXPECT_EQ(span.key_offset, 0);
  EXPECT_EQ(span.value_offset, 0);
  ASSERT_EQ(span.token_readings.size(), 2);
  EXPECT_EQ(span.token_readings[0], "みかみ");
  EXPECT_EQ(span.token_readings[1], "あい");
  EXPECT_TRUE(span.verified_personal_full_name);
}

TEST_F(NBestGeneratorTest, GeneralNounNotProtectedWithoutExactNamePair) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  Lattice lattice;
  lattice.SetKey("みかみあい");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = fixture->GetPosMatcher().GetGeneralNounId();
  lattice.Insert(0, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = fixture->GetPosMatcher().GetLastNameId();
  lattice.Insert(0, surname);
  // The given-name reading matches, but its surface does NOT.
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "アイ";
  given->lid = given->rid = fixture->GetPosMatcher().GetFirstNameId();
  lattice.Insert(std::string("みかみ").size(), given);

  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  auto generator = fixture->CreateNBestGenerator(lattice, request);
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  EXPECT_TRUE(candidate.proper_noun_spans.empty());
}

TEST_F(NBestGeneratorTest, UserDictionaryDecompositionCannotProveFullName) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  Lattice lattice;
  lattice.SetKey("みかみあい");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = fixture->GetPosMatcher().GetGeneralNounId();
  lattice.Insert(0, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = fixture->GetPosMatcher().GetLastNameId();
  surname->attributes |= Node::USER_DICTIONARY;
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = fixture->GetPosMatcher().GetFirstNameId();
  lattice.Insert(std::string("みかみ").size(), given);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  auto generator = fixture->CreateNBestGenerator(lattice, request);
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  EXPECT_TRUE(candidate.proper_noun_spans.empty());
}

TEST_F(NBestGeneratorTest, FullNameDictionaryNodeCarriesStrongEvidence) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  Lattice lattice;
  lattice.SetKey("さとうあい");
  auto* full = lattice.NewNode();
  full->key = "さとうあい";
  full->value = "佐藤愛";
  full->lid = fixture->GetPosMatcher().GetLastNameId();
  full->rid = fixture->GetPosMatcher().GetFirstNameId();
  lattice.Insert(0, full);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  auto generator = fixture->CreateNBestGenerator(lattice, request);
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {full};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  EXPECT_TRUE(candidate.proper_noun_spans[0].verified_personal_full_name);
}

TEST_F(NBestGeneratorTest, TwoSelectedNameNodesFormOneStrongFullName) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  Lattice lattice;
  lattice.SetKey("みかみあい");
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = fixture->GetPosMatcher().GetLastNameId();
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = fixture->GetPosMatcher().GetFirstNameId();
  lattice.Insert(std::string("みかみ").size(), given);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  auto generator = fixture->CreateNBestGenerator(lattice, request);
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {surname, given};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  EXPECT_EQ(candidate.proper_noun_spans[0].value, "見上愛");
  EXPECT_TRUE(candidate.proper_noun_spans[0].verified_personal_full_name);
}

TEST_F(NBestGeneratorTest, AmbiguousSurnameGivenSplitsAreNotPromoted) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  Lattice lattice;
  lattice.SetKey("みかみあい");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = fixture->GetPosMatcher().GetGeneralNounId();
  lattice.Insert(0, whole);
  auto* family1 = lattice.NewNode();
  family1->key = "みかみ";
  family1->value = "見上";
  family1->lid = family1->rid = fixture->GetPosMatcher().GetLastNameId();
  lattice.Insert(0, family1);
  auto* name1 = lattice.NewNode();
  name1->key = "あい";
  name1->value = "愛";
  name1->lid = name1->rid = fixture->GetPosMatcher().GetFirstNameId();
  lattice.Insert(std::string("みかみ").size(), name1);
  auto* family2 = lattice.NewNode();
  family2->key = "み";
  family2->value = "見";
  family2->lid = family2->rid = fixture->GetPosMatcher().GetLastNameId();
  lattice.Insert(0, family2);
  auto* name2 = lattice.NewNode();
  name2->key = "かみあい";
  name2->value = "上愛";
  name2->lid = name2->rid = fixture->GetPosMatcher().GetFirstNameId();
  lattice.Insert(std::string("み").size(), name2);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  auto generator = fixture->CreateNBestGenerator(lattice, request);
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  EXPECT_TRUE(candidate.proper_noun_spans.empty());
}

TEST_F(NBestGeneratorTest, SingleAmbiguousGivenNameIsNotStrongFullName) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  Lattice lattice;
  lattice.SetKey("あい");
  auto* single = lattice.NewNode();
  single->key = "あい";
  single->value = "愛";
  single->lid = single->rid = fixture->GetPosMatcher().GetFirstNameId();
  lattice.Insert(0, single);
  const ConversionRequest request = ConvReq(ConversionRequest::CONVERSION);
  auto generator = fixture->CreateNBestGenerator(lattice, request);
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {single};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  EXPECT_FALSE(candidate.proper_noun_spans[0].verified_personal_full_name);
}


// v8.2: Evidence must survive a following particle, without protecting it.
TEST_F(NBestGeneratorTest, GeneralNounFullNameBeforeParticle) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  const auto& pos = fixture->GetPosMatcher();
  Lattice lattice;
  lattice.SetKey("みかみあいが");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = pos.GetGeneralNounId();
  lattice.Insert(0, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = pos.GetLastNameId();
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = pos.GetFirstNameId();
  lattice.Insert(surname->key.size(), given);
  auto* particle = lattice.NewNode();
  particle->key = "が";
  particle->value = "が";
  particle->lid = particle->rid = pos.GetGeneralNounId();
  lattice.Insert(whole->key.size(), particle);
  auto generator = fixture->CreateNBestGenerator(
      lattice, ConvReq(ConversionRequest::CONVERSION));
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole, particle};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  const auto& span = candidate.proper_noun_spans[0];
  EXPECT_EQ(span.key, "みかみあい");
  EXPECT_EQ(span.value, "見上愛");
  EXPECT_EQ(span.key_offset, 0);
  EXPECT_EQ(span.value_offset, 0);
  EXPECT_TRUE(span.verified_personal_full_name);
  EXPECT_EQ(candidate.value, "見上愛が");
}

TEST_F(NBestGeneratorTest, EmbeddedNameAtSelectedNodeBoundaryHasOffsets) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  const auto& pos = fixture->GetPosMatcher();
  Lattice lattice;
  lattice.SetKey("きょうはみかみあいが");
  auto* prefix = lattice.NewNode();
  prefix->key = "きょうは";
  prefix->value = "今日は";
  prefix->lid = prefix->rid = pos.GetGeneralNounId();
  lattice.Insert(0, prefix);
  const auto name_offset = prefix->key.size();
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = pos.GetGeneralNounId();
  lattice.Insert(name_offset, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = pos.GetLastNameId();
  lattice.Insert(name_offset, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = pos.GetFirstNameId();
  lattice.Insert(name_offset + surname->key.size(), given);
  auto* particle = lattice.NewNode();
  particle->key = "が";
  particle->value = "が";
  particle->lid = particle->rid = pos.GetGeneralNounId();
  lattice.Insert(name_offset + whole->key.size(), particle);
  auto generator = fixture->CreateNBestGenerator(
      lattice, ConvReq(ConversionRequest::CONVERSION));
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {prefix, whole, particle};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  EXPECT_EQ(candidate.proper_noun_spans[0].value, "見上愛");
  EXPECT_EQ(candidate.proper_noun_spans[0].key_offset, prefix->key.size());
  EXPECT_EQ(candidate.proper_noun_spans[0].value_offset, prefix->value.size());
  EXPECT_TRUE(candidate.proper_noun_spans[0].verified_personal_full_name);
}

// Even if a whole candidate is one general-noun node, a validated external
// functional suffix boundary permits guarding only the name prefix.
TEST_F(NBestGeneratorTest, WholeNodeWithFunctionalSuffixCanGuardNamePrefix) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  const auto& pos = fixture->GetPosMatcher();
  Lattice lattice;
  lattice.SetKey("みかみあいが");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあいが";
  whole->value = "見上愛が";
  whole->lid = whole->rid = pos.GetGeneralNounId();
  lattice.Insert(0, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = pos.GetLastNameId();
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = pos.GetFirstNameId();
  lattice.Insert(surname->key.size(), given);
  auto* functional = lattice.NewNode();
  functional->key = "が";
  functional->value = "が";
  // Functional suffix should be a grammatical particle in real data.  The
  // mock table offers a stable functional POS through the rule getter.
  functional->lid = functional->rid = pos.GetFunctionalId();
  lattice.Insert(surname->key.size() + given->key.size(), functional);
  auto generator = fixture->CreateNBestGenerator(
      lattice, ConvReq(ConversionRequest::CONVERSION));
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  EXPECT_EQ(candidate.proper_noun_spans[0].key, "みかみあい");
  EXPECT_EQ(candidate.proper_noun_spans[0].value, "見上愛");
}

TEST_F(NBestGeneratorTest, WholeNodeWithoutFunctionalSuffixRemainsUnguarded) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  const auto& pos = fixture->GetPosMatcher();
  Lattice lattice;
  lattice.SetKey("みかみあいが");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあいが";
  whole->value = "見上愛が";
  whole->lid = whole->rid = pos.GetGeneralNounId();
  lattice.Insert(0, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = pos.GetLastNameId();
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = pos.GetFirstNameId();
  lattice.Insert(surname->key.size(), given);
  auto generator = fixture->CreateNBestGenerator(
      lattice, ConvReq(ConversionRequest::CONVERSION));
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  EXPECT_TRUE(candidate.proper_noun_spans.empty());
}


// v8.3: A large number of unrelated nodes at the surname's start position
// must not suppress exact surname+given-name evidence. v8.2's raw 192-node
// traversal budget rejected this case before reaching the surname node.
TEST_F(NBestGeneratorTest, UnrelatedPrefixNodesCannotExhaustNameEvidence) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  const auto& pos = fixture->GetPosMatcher();
  Lattice lattice;
  lattice.SetKey("みかみあいの");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = pos.GetGeneralNounId();
  lattice.Insert(0, whole);
  for (int i = 0; i < 210; ++i) {
    auto* unrelated = lattice.NewNode();
    unrelated->key = "み";
    unrelated->value = "み";
    unrelated->lid = unrelated->rid = pos.GetGeneralNounId();
    lattice.Insert(0, unrelated);
  }
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = pos.GetLastNameId();
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = pos.GetFirstNameId();
  lattice.Insert(surname->key.size(), given);
  auto* particle = lattice.NewNode();
  particle->key = "の";
  particle->value = "の";
  particle->lid = particle->rid = pos.GetGeneralNounId();
  lattice.Insert(whole->key.size(), particle);

  auto generator = fixture->CreateNBestGenerator(
      lattice, ConvReq(ConversionRequest::CONVERSION));
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole, particle};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  EXPECT_EQ(candidate.proper_noun_spans[0].key, "みかみあい");
  EXPECT_EQ(candidate.proper_noun_spans[0].value, "見上愛");
  EXPECT_TRUE(candidate.proper_noun_spans[0].verified_personal_full_name);
  EXPECT_EQ(candidate.value, "見上愛の");
}

// v8.3: v8.2 found the exact given name but discarded it because unrelated
// candidates at its position pushed the total examined count above 192.
TEST_F(NBestGeneratorTest, UnrelatedGivenPositionNodesCannotDiscardNameEvidence) {
  auto fixture = std::make_unique<MockDataAndImmutableConverter>();
  const auto& pos = fixture->GetPosMatcher();
  Lattice lattice;
  lattice.SetKey("みかみあいから");
  auto* whole = lattice.NewNode();
  whole->key = "みかみあい";
  whole->value = "見上愛";
  whole->lid = whole->rid = pos.GetGeneralNounId();
  lattice.Insert(0, whole);
  auto* surname = lattice.NewNode();
  surname->key = "みかみ";
  surname->value = "見上";
  surname->lid = surname->rid = pos.GetLastNameId();
  lattice.Insert(0, surname);
  auto* given = lattice.NewNode();
  given->key = "あい";
  given->value = "愛";
  given->lid = given->rid = pos.GetFirstNameId();
  lattice.Insert(surname->key.size(), given);
  for (int i = 0; i < 210; ++i) {
    auto* unrelated = lattice.NewNode();
    unrelated->key = "あ";
    unrelated->value = "あ";
    unrelated->lid = unrelated->rid = pos.GetGeneralNounId();
    lattice.Insert(surname->key.size(), unrelated);
  }
  auto* particle = lattice.NewNode();
  particle->key = "から";
  particle->value = "から";
  particle->lid = particle->rid = pos.GetGeneralNounId();
  lattice.Insert(whole->key.size(), particle);

  auto generator = fixture->CreateNBestGenerator(
      lattice, ConvReq(ConversionRequest::CONVERSION));
  NBestGeneratorTestPeer peer(*generator);
  const std::vector<const Node*> selected = {whole, particle};
  Candidate candidate;
  peer.MakeCandidate(candidate, 0, 0, 0,
                     absl::Span<const Node* const>(selected.data(),
                                                   selected.size()));
  ASSERT_EQ(candidate.proper_noun_spans.size(), 1);
  EXPECT_EQ(candidate.proper_noun_spans[0].key, "みかみあい");
  EXPECT_EQ(candidate.proper_noun_spans[0].value, "見上愛");
  EXPECT_TRUE(candidate.proper_noun_spans[0].verified_personal_full_name);
  EXPECT_EQ(candidate.value, "見上愛から");
}

}  // namespace mozc
