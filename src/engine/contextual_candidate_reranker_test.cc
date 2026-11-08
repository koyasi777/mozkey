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
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// INCLUDING NEGLIGENCE OR OTHERWISE ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "engine/contextual_candidate_reranker.h"

#include <string>

#include "absl/strings/string_view.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "testing/gunit.h"

namespace mozc {
namespace engine {
namespace {

using ::mozc::converter::Candidate;
using ::mozc::converter::Segment;
using ::mozc::converter::Segments;

Candidate* AddCandidate(Segment* segment, absl::string_view key,
                        absl::string_view value, int cost) {
  Candidate* candidate = segment->add_candidate();
  candidate->key = std::string(key);
  candidate->content_key = std::string(key);
  candidate->value = std::string(value);
  candidate->content_value = std::string(value);
  candidate->cost = cost;
  return candidate;
}

Segment* AddHistorySegment(Segments* segments, absl::string_view key,
                           absl::string_view value) {
  Segment* segment = segments->add_segment();
  segment->set_segment_type(Segment::HISTORY);
  segment->set_key(key);

  Candidate* candidate = segment->add_candidate();
  candidate->key = std::string(key);
  candidate->content_key = std::string(key);
  candidate->value = std::string(value);
  candidate->content_value = std::string(value);
  candidate->cost = 0;
  return segment;
}

Segment* AddConversionSegment(Segments* segments, absl::string_view key) {
  Segment* segment = segments->add_segment();
  segment->set_segment_type(Segment::FREE);
  segment->set_key(key);
  return segment;
}

void AddParticleSegmentWithBadTopCandidate(Segments* segments,
                                           absl::string_view particle_key,
                                           absl::string_view bad_value) {
  Segment* segment = AddConversionSegment(segments, particle_key);

  // Bad homophone candidate is initially on top.
  AddCandidate(segment, particle_key, bad_value, 5000);

  // Plain particle candidate is initially worse by cost.
  AddCandidate(segment, particle_key, particle_key, 9500);

  // Other noisy candidates.
  AddCandidate(segment, particle_key, "似", 7000);
  AddCandidate(segment, particle_key, "2", 5100);
}

void AddKatakanaNextSegment(Segments* segments, absl::string_view key,
                            absl::string_view value) {
  Segment* segment = AddConversionSegment(segments, key);
  AddCandidate(segment, key, value, 7000);
}

void ExpectParticlePromotedAfterHistory(absl::string_view particle_key,
                                        absl::string_view bad_value) {
  Segments segments;
  AddHistorySegment(&segments, "とうきょう", "東京");
  AddParticleSegmentWithBadTopCandidate(&segments, particle_key, bad_value);
  AddKatakanaNextSegment(&segments, "まーじ", "マージ");

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(0).key(), particle_key);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, bad_value);

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  EXPECT_EQ(segments.conversion_segment(0).key(), particle_key);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, particle_key);
}

void ExpectFunctionalParticleExpressionPromotedAfterHistory(
    absl::string_view expression_key, absl::string_view bad_value) {
  Segments segments;
  AddHistorySegment(&segments, "github", "github");

  Segment* segment = AddConversionSegment(&segments, expression_key);

  // Bad homophone/numeric candidate is initially on top.
  AddCandidate(segment, expression_key, bad_value, 5200);

  // Plain functional expression is initially worse by cost.
  AddCandidate(segment, expression_key, expression_key, 6000);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  ASSERT_EQ(segments.conversion_segment(0).key(), expression_key);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, bad_value);

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  EXPECT_EQ(segments.conversion_segment(0).key(), expression_key);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value,
            expression_key);
}

TEST(ContextualCandidateRerankerTest, PromotesNiParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("に", "二");
}

TEST(ContextualCandidateRerankerTest, PromotesWoParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("を", "尾");
}

TEST(ContextualCandidateRerankerTest, PromotesGaParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("が", "画");
}

TEST(ContextualCandidateRerankerTest, PromotesHaParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("は", "歯");
}

TEST(ContextualCandidateRerankerTest, PromotesHeParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("へ", "辺");
}

TEST(ContextualCandidateRerankerTest, PromotesDeParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("で", "出");
}

TEST(ContextualCandidateRerankerTest, PromotesToParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("と", "戸");
}

TEST(ContextualCandidateRerankerTest, PromotesNoParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("の", "野");
}

TEST(ContextualCandidateRerankerTest, PromotesMoParticleAfterHistory) {
  ExpectParticlePromotedAfterHistory("も", "藻");
}

TEST(ContextualCandidateRerankerTest,
     PromotesNihaFunctionalExpressionAfterAsciiHistory) {
  ExpectFunctionalParticleExpressionPromotedAfterHistory("には", "二は");
}

TEST(ContextualCandidateRerankerTest,
     PromotesNimoFunctionalExpressionAfterAsciiHistory) {
  ExpectFunctionalParticleExpressionPromotedAfterHistory("にも", "二も");
}

TEST(ContextualCandidateRerankerTest,
     PromotesDehaFunctionalExpressionAfterAsciiHistory) {
  ExpectFunctionalParticleExpressionPromotedAfterHistory("では", "出は");
}

TEST(ContextualCandidateRerankerTest,
     PromotesDemoFunctionalExpressionAfterAsciiHistory) {
  ExpectFunctionalParticleExpressionPromotedAfterHistory("でも", "出も");
}

TEST(ContextualCandidateRerankerTest,
     PromotesTohaFunctionalExpressionAfterAsciiHistory) {
  ExpectFunctionalParticleExpressionPromotedAfterHistory("とは", "戸は");
}

TEST(ContextualCandidateRerankerTest, DoesNotPromoteWithoutHistory) {
  Segments segments;
  AddParticleSegmentWithBadTopCandidate(&segments, "に", "二");
  AddKatakanaNextSegment(&segments, "まーじ", "マージ");

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "二");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二");
}

TEST(ContextualCandidateRerankerTest,
     DoesNotPromoteFunctionalParticleExpressionWithoutHistory) {
  Segments segments;

  Segment* segment = AddConversionSegment(&segments, "には");
  AddCandidate(segment, "には", "二は", 5200);
  AddCandidate(segment, "には", "には", 6000);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "二は");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二は");
}

TEST(ContextualCandidateRerankerTest, PromotesAfterPreviousConversionSegment) {
  Segments segments;

  Segment* previous = AddConversionSegment(&segments, "とうきょう");
  AddCandidate(previous, "とうきょう", "東京", 4000);

  AddParticleSegmentWithBadTopCandidate(&segments, "に", "二");
  AddKatakanaNextSegment(&segments, "まーじ", "マージ");

  ASSERT_EQ(segments.conversion_segments_size(), 3);
  ASSERT_EQ(segments.conversion_segment(1).key(), "に");
  ASSERT_EQ(segments.conversion_segment(1).candidate(0).value, "二");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "に");
}

TEST(ContextualCandidateRerankerTest,
     PromotesFunctionalParticleExpressionAfterKanjiHistory) {
  Segments segments;
  AddHistorySegment(&segments, "とうきょう", "東京");

  Segment* segment = AddConversionSegment(&segments, "には");
  AddCandidate(segment, "には", "二は", 5200);
  AddCandidate(segment, "には", "には", 6000);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "二は");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "には");
}

TEST(ContextualCandidateRerankerTest,
     PromotesFunctionalParticleExpressionAfterPreviousConversionSegment) {
  Segments segments;

  Segment* previous = AddConversionSegment(&segments, "とうきょう");
  AddCandidate(previous, "とうきょう", "東京", 4000);

  Segment* segment = AddConversionSegment(&segments, "には");
  AddCandidate(segment, "には", "二は", 5200);
  AddCandidate(segment, "には", "には", 6000);

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(1).key(), "には");
  ASSERT_EQ(segments.conversion_segment(1).candidate(0).value, "二は");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "には");
}

TEST(ContextualCandidateRerankerTest, DoesNotTouchNonParticleSegment) {
  Segments segments;
  AddHistorySegment(&segments, "とうきょう", "東京");

  Segment* segment = AddConversionSegment(&segments, "まーじ");
  AddCandidate(segment, "まーじ", "マージ", 7000);
  AddCandidate(segment, "まーじ", "まーじ", 9000);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "マージ");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "マージ");
}

TEST(ContextualCandidateRerankerTest, DoesNotTouchParticleWithoutPlainCandidate) {
  Segments segments;
  AddHistorySegment(&segments, "とうきょう", "東京");

  Segment* segment = AddConversionSegment(&segments, "に");
  AddCandidate(segment, "に", "二", 5000);
  AddCandidate(segment, "に", "似", 7000);
  AddKatakanaNextSegment(&segments, "まーじ", "マージ");

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "二");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二");
}

TEST(ContextualCandidateRerankerTest, DoesNotTouchWhenParticleAlreadyTop) {
  Segments segments;
  AddHistorySegment(&segments, "とうきょう", "東京");

  Segment* segment = AddConversionSegment(&segments, "に");
  AddCandidate(segment, "に", "に", 5000);
  AddCandidate(segment, "に", "二", 9500);
  AddKatakanaNextSegment(&segments, "まーじ", "マージ");

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "に");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に");
}

TEST(ContextualCandidateRerankerTest,
     PromotesPlainParticleOverDakutenKanaAfterAsciiHistory) {
  Segments segments;
  AddHistorySegment(&segments, "main", "main");

  Segment* particle = AddConversionSegment(&segments, "に");

  // Regression case:
  // "に゛" may have a much lower raw cost than the plain particle "に".
  // Contextual reranking must still prefer the plain particle after noun-like
  // ASCII history.
  AddCandidate(particle, "に", "に゛", 4258);
  AddCandidate(particle, "に", "に", 9352);
  AddCandidate(particle, "に", "二", 4258);

  AddKatakanaNextSegment(&segments, "まーじ", "マージ");

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(0).key(), "に");
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "に゛");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に");
}

TEST(ContextualCandidateRerankerTest,
     PromotesShikaNegativePrefixAfterCounterExpression) {
  Segments segments;

  Segment* number = AddConversionSegment(&segments, "2");
  AddCandidate(number, "2", "2", 1000);

  Segment* counter = AddConversionSegment(&segments, "めい");
  AddCandidate(counter, "めい", "名", 1000);

  Segment* shika = AddConversionSegment(&segments, "しかい");

  // Regression case:
  //   2名 + しかい...
  // should not become:
  //   2名 + 司会 / 視界 / 歯科医
  AddCandidate(shika, "しかい", "司会", 6401);
  AddCandidate(shika, "しかい", "視界", 6461);
  AddCandidate(shika, "しかい", "歯科医", 7459);

  // The functional-expression path is initially much worse by raw cost.
  AddCandidate(shika, "しかい", "しかい", 12422);

  ASSERT_EQ(segments.conversion_segments_size(), 3);
  ASSERT_EQ(segments.conversion_segment(2).candidate(0).value, "司会");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(2).candidate(0).value, "しかい");
}

TEST(ContextualCandidateRerankerTest,
     PromotesCounterPlusShikaOverHomophoneCompounds) {
  Segments segments;

  Segment* number = AddConversionSegment(&segments, "2");
  AddCandidate(number, "2", "2", 1000);

  Segment* shika = AddConversionSegment(&segments, "めいしか");

  // Bad compound-like candidates are initially on top.
  AddCandidate(shika, "めいしか", "名歯科", 5000);
  AddCandidate(shika, "めいしか", "名士か", 5200);

  // Desired functional expression.
  AddCandidate(shika, "めいしか", "名しか", 9000);

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(1).candidate(0).value, "名歯科");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "名しか");
}

TEST(ContextualCandidateRerankerTest,
     DoesNotDemoteShikaiAfterNoAttributiveContext) {
  Segments segments;

  Segment* previous = AddConversionSegment(&segments, "つぎの");
  AddCandidate(previous, "つぎの", "次の", 1000);

  Segment* shikai = AddConversionSegment(&segments, "しかい");
  AddCandidate(shikai, "しかい", "司会", 6401);
  AddCandidate(shikai, "しかい", "しかい", 12422);

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(1).candidate(0).value, "司会");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "司会");
}

TEST(ContextualCandidateRerankerTest,
     PromotesShikaNegativePrefixAfterCounterHistory) {
  Segments segments;

  AddHistorySegment(&segments, "2めい", "2名");

  Segment* shika = AddConversionSegment(&segments, "しかい");
  AddCandidate(shika, "しかい", "司会", 6401);
  AddCandidate(shika, "しかい", "視界", 6461);
  AddCandidate(shika, "しかい", "しかい", 12422);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "司会");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "しかい");
}

TEST(ContextualCandidateRerankerTest,
     PromotesShikaExplicitNegativeAfterNoContext) {
  Segments segments;

  Segment* previous = AddConversionSegment(&segments, "やまだの");
  AddCandidate(previous, "やまだの", "山田の", 1000);

  Segment* shika = AddConversionSegment(&segments, "しかな");
  AddCandidate(shika, "しかな", "鹿名", 5000);
  AddCandidate(shika, "しかな", "歯科名", 5200);
  AddCandidate(shika, "しかな", "しかな", 12422);

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(1).candidate(0).value, "鹿名");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "しかな");
}

TEST(ContextualCandidateRerankerTest,
     DoesNotDemoteShikaiAfterNoContext) {
  Segments segments;

  Segment* previous = AddConversionSegment(&segments, "やまだの");
  AddCandidate(previous, "やまだの", "山田の", 1000);

  Segment* shikai = AddConversionSegment(&segments, "しかい");
  AddCandidate(shikai, "しかい", "司会", 6401);
  AddCandidate(shikai, "しかい", "しかい", 12422);

  ASSERT_EQ(segments.conversion_segments_size(), 2);
  ASSERT_EQ(segments.conversion_segment(1).candidate(0).value, "司会");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "司会");
}

TEST(ContextualCandidateRerankerTest,
     PromotesShikaNegativePrefixAfterNounLikeHistory) {
  Segments segments;

  AddHistorySegment(&segments, "やまだ", "山田");

  Segment* shika = AddConversionSegment(&segments, "しかい");
  AddCandidate(shika, "しかい", "司会", 6401);
  AddCandidate(shika, "しかい", "視界", 6461);
  AddCandidate(shika, "しかい", "しかい", 12422);

  ASSERT_EQ(segments.conversion_segments_size(), 1);
  ASSERT_EQ(segments.conversion_segment(0).candidate(0).value, "司会");

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);

  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "しかい");
}

TEST(ContextualCandidateRerankerTest,
     PromotesNiShikaAfterCommittedAsciiClosingParen) {
  Segments segments;
  AddHistorySegment(&segments, ")", ")");
  Segment* segment = AddConversionSegment(&segments, "にしかできない");
  AddCandidate(segment, "にしかできない", "二しかできない", 5200);
  Candidate* plain =
      AddCandidate(segment, "にしかできない", "にしかできない", 6100);
  // Mirror the real N-best distinction between a content numeral and a
  // function-word prefix, rather than using the synthetic full surface.
  segment->mutable_candidate(0)->content_value = "二";
  plain->content_value = "に";

  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value,
            "にしかできない");
}

TEST(ContextualCandidateRerankerTest,
     PromotesNiShikaAfterCommittedFullwidthClosingParen) {
  Segments segments;
  AddHistorySegment(&segments, "（かんりしゃ）", "（管理者）");
  Segment* segment = AddConversionSegment(&segments, "にしか");
  AddCandidate(segment, "にしか", "二しか", 5000);
  AddCandidate(segment, "にしか", "にしか", 6500);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "にしか");
}

TEST(ContextualCandidateRerankerTest,
     PromotesSplitNiBeforeShikaAfterClosingParen) {
  Segments segments;
  AddHistorySegment(&segments, ")", ")");
  AddParticleSegmentWithBadTopCandidate(&segments, "に", "二");
  Segment* next = AddConversionSegment(&segments, "しかできない");
  AddCandidate(next, "しかできない", "しかできない", 6500);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に");
}

TEST(ContextualCandidateRerankerTest,
     PromotesNiShikaAfterPreviousConversionClosingParen) {
  Segments segments;
  Segment* bracket = AddConversionSegment(&segments, ")");
  AddCandidate(bracket, ")", ")", 500);
  Segment* segment = AddConversionSegment(&segments, "にしか");
  AddCandidate(segment, "にしか", "二しか", 5200);
  AddCandidate(segment, "にしか", "にしか", 6200);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "にしか");
}

TEST(ContextualCandidateRerankerTest,
     PromotesNiShikaAfterCounterPhraseWhenConnected) {
  Segments segments;
  AddHistorySegment(&segments, "ふたり", "2人");
  Segment* segment = AddConversionSegment(&segments, "にしかできない");
  AddCandidate(segment, "にしかできない", "二しかできない", 5000);
  AddCandidate(segment, "にしかできない", "にしかできない", 7500);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value,
            "にしかできない");
}

TEST(ContextualCandidateRerankerTest,
     DoesNotPromoteWithoutHistoryOrPlainCandidate) {
  {
    Segments segments;
    // Without a history segment, neither the existing particle rule nor
    // the new bracket-specific rule has left-context evidence.
    AddParticleSegmentWithBadTopCandidate(&segments, "に", "二");
    Segment* next = AddConversionSegment(&segments, "いく");
    AddCandidate(next, "いく", "行く", 6500);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二");
  }
  {
    Segments segments;
    AddHistorySegment(&segments, ")", ")");
    Segment* segment = AddConversionSegment(&segments, "にしか");
    AddCandidate(segment, "にしか", "二しか", 5000);
    AddCandidate(segment, "にしか", "弐しか", 6000);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二しか");
  }
}

TEST(ContextualCandidateRerankerTest,
     PromotesNiShikaAcrossCommonLeftContextScriptsAndBoundaries) {
  constexpr absl::string_view kContexts[] = {
      "(", ")", "（", "）", "（管理者）", "[管理者]", "」", "\"",
      " ", "　", "API", "GitHub ", "v2.0", "管理者", "API (beta) "};
  for (const absl::string_view context : kContexts) {
    Segments segments;
    AddHistorySegment(&segments, context, context);
    Segment* segment = AddConversionSegment(&segments, "にしかできない");
    AddCandidate(segment, "にしかできない", "二しかできない", 5000);
    Candidate* plain =
        AddCandidate(segment, "にしかできない", "にしかできない", 6500);
    // Reproduce content_value != value for a candidate containing particles.
    segment->mutable_candidate(0)->content_value = "二";
    plain->content_value = "に";
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value,
              "にしかできない") << std::string(context);
  }
}

TEST(ContextualCandidateRerankerTest,
     PromotesNiShikaUsingClientContextWithoutHistory) {
  for (absl::string_view context : {
           "(", ")", "（", "）", " ", "API", "（管理者）"}) {
    Segments segments;
    Segment* segment = AddConversionSegment(&segments, "にしかできない");
    AddCandidate(segment, "にしかできない", "2しかできない", 5000);
    AddCandidate(segment, "にしかできない", "にしかできない", 7500);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, context);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value,
              "にしかできない") << std::string(context);
  }
}

TEST(ContextualCandidateRerankerTest,
     PromotesNiShikaAfterPreviousConversionAsciiContext) {
  Segments segments;
  Segment* previous = AddConversionSegment(&segments, "api");
  AddCandidate(previous, "api", "API", 1000);
  Segment* segment = AddConversionSegment(&segments, "にしか");
  AddCandidate(segment, "にしか", "二しか", 5000);
  AddCandidate(segment, "にしか", "にしか", 7000);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "にしか");
}

TEST(ContextualCandidateRerankerTest,
     PromotesSplitNiShikaAfterOpeningParen) {
  Segments segments;
  AddHistorySegment(&segments, "(", "(");
  AddParticleSegmentWithBadTopCandidate(&segments, "に", "二");
  Segment* continuation = AddConversionSegment(&segments, "しかできない");
  AddCandidate(continuation, "しかできない", "しかできない", 6000);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に");
}

TEST(ContextualCandidateRerankerTest,
     KeepsStandaloneNumberAndIndependentClauseBoundary) {
  for (absl::string_view context : {"", "前文。", "前文！", "前文?"}) {
    Segments segments;
    Segment* segment = AddConversionSegment(&segments, "にしかできない");
    AddCandidate(segment, "にしかできない", "二しかできない", 5000);
    AddCandidate(segment, "にしかできない", "にしかできない", 6500);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, context);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value,
              "二しかできない") << std::string(context);
  }
}

TEST(ContextualCandidateRerankerTest,
     DoesNotInventAbsentKanaCandidateForNiShika) {
  Segments segments;
  AddHistorySegment(&segments, "API", "API");
  Segment* segment = AddConversionSegment(&segments, "にしかできない");
  AddCandidate(segment, "にしかできない", "二しかできない", 5000);
  AddCandidate(segment, "にしかできない", "2しかできない", 5200);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value,
            "二しかできない");
}

TEST(ContextualCandidateRerankerTest,
     NiShikaUsesNewestHistoryNotOldestOrStaleClientContext) {
  {
    Segments segments;
    AddHistorySegment(&segments, "かんりしゃ", "管理者");
    AddHistorySegment(&segments, "。", "。");
    Segment* segment = AddConversionSegment(&segments, "にしかできない");
    AddCandidate(segment, "にしかできない", "二しかできない", 5000);
    AddCandidate(segment, "にしかできない", "にしかできない", 7500);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, "API");
    EXPECT_EQ(segment->candidate(0).value, "二しかできない");
  }
  {
    Segments segments;
    AddHistorySegment(&segments, "。", "。");
    AddHistorySegment(&segments, "api", "API");
    Segment* segment = AddConversionSegment(&segments, "にしかできない");
    AddCandidate(segment, "にしかできない", "二しかできない", 5000);
    AddCandidate(segment, "にしかできない", "にしかできない", 7500);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segment->candidate(0).value, "にしかできない");
  }
}

TEST(ContextualCandidateRerankerTest, NiShikaDoesNotOverrideProperNames) {
  for (absl::string_view key : {"にしかわ", "にしかい", "にしかな"}) {
    Segments segments;
    AddHistorySegment(&segments, "かぶしきがいしゃ", "株式会社");
    Segment* segment = AddConversionSegment(&segments, key);
    AddCandidate(segment, key, "西川", 5000);
    AddCandidate(segment, key, key, 6500);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segment->candidate(0).value, "西川") << std::string(key);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiShikaDoesNotBiasQuantityAfterTopicMarkers) {
  for (absl::string_view context : {"プランは", "使えるのは", "候補も", "数量なら"}) {
    Segments segments;
    AddHistorySegment(&segments, context, context);
    Segment* segment = AddConversionSegment(&segments, "にしかない");
    AddCandidate(segment, "にしかない", "二しかない", 5000);
    AddCandidate(segment, "にしかない", "にしかない", 6000);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segment->candidate(0).value, "二しかない")
        << std::string(context);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiShikaDoesNotBiasQuantityAfterOnlySpaceOrBracket) {
  for (absl::string_view context : {" ", "　", "(", ")", "（", "）"}) {
    Segments segments;
    Segment* segment = AddConversionSegment(&segments, "にしかない");
    AddCandidate(segment, "にしかない", "2しかない", 5000);
    AddCandidate(segment, "にしかない", "にしかない", 6000);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, context);
    EXPECT_EQ(segment->candidate(0).value, "2しかない")
        << std::string(context);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiShikaRequiresNumeralCompetitorBeforeReranking) {
  Segments segments;
  AddHistorySegment(&segments, "API", "API");
  Segment* segment = AddConversionSegment(&segments, "にしかできない");
  AddCandidate(segment, "にしかできない", "ニシカデキナイ", 4800);
  AddCandidate(segment, "にしかできない", "にしかできない", 6000);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segment->candidate(0).value, "ニシカデキナイ");
}

TEST(ContextualCandidateRerankerTest,
     NiShikaPromotesLiveNegativePredicatePrefixes) {
  for (absl::string_view key : {"にしか", "にしかで", "にしかでき",
                                 "にしかできな", "にしかできない",
                                 "にしかいな", "にしかいない"}) {
    Segments segments;
    AddHistorySegment(&segments, "API", "API");
    Segment* segment = AddConversionSegment(&segments, key);
    std::string numeric = "二しか";
    const absl::string_view tail =
        key.substr(absl::string_view("にしか").size());
    numeric.append(tail.data(), tail.size());
    AddCandidate(segment, key, numeric, 5000);
    AddCandidate(segment, key, key, 7000);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segment->candidate(0).value, key) << std::string(key);
  }
}

// A complete demonstrative phrase gives stronger grammatical evidence for
// a case particle than a standalone two (二).  This is an explicit fallback
// for client context when the committed text is absent from Mozc HISTORY.
TEST(ContextualCandidateRerankerTest,
     NiBeforeDemonstrativeUsesClientContextWithoutHistory) {
  for (absl::string_view left : {"()", "（管理者）", "API", "API ",
                                "（", ")", "文章"}) {
    Segments segments;
    Segment* ni = AddConversionSegment(&segments, "に");
    AddCandidate(ni, "に", "二", 7959);
    AddCandidate(ni, "に", "に", 8419);
    Segment* sono = AddConversionSegment(&segments, "その");
    AddCandidate(sono, "その", "その", 4910);
    Segment* content = AddConversionSegment(&segments, "ないようを");
    AddCandidate(content, "ないようを", "内容を", 4586);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, left);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に")
        << std::string(left);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiBeforeDemonstrativeDoesNotOverrideHardBoundariesOrTopicFrames) {
  for (absl::string_view left : {"", " ", "前文。", "API!", "API?",
                                "候補は", "候補も", "候補なら"}) {
    Segments segments;
    Segment* ni = AddConversionSegment(&segments, "に");
    AddCandidate(ni, "に", "二", 7959);
    AddCandidate(ni, "に", "に", 8419);
    Segment* sono = AddConversionSegment(&segments, "その");
    AddCandidate(sono, "その", "その", 4910);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, left);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二")
        << std::string(left);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiExternalContextDoesNotOverrideNumbersWithoutDemonstrative) {
  for (absl::string_view next_key : {"じかん", "ひと", "しかない"}) {
    Segments segments;
    Segment* ni = AddConversionSegment(&segments, "に");
    AddCandidate(ni, "に", "二", 7959);
    AddCandidate(ni, "に", "に", 8419);
    Segment* next = AddConversionSegment(&segments, next_key);
    AddCandidate(next, next_key, next_key, 4910);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, "()");
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二")
        << std::string(next_key);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiExternalContextDoesNotOverrideStandaloneNumber) {
  Segments segments;
  Segment* ni = AddConversionSegment(&segments, "に");
  AddCandidate(ni, "に", "二", 4506);
  AddCandidate(ni, "に", "に", 4891);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments, "()");
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二");
}

// Bracket pairs contain no noun.  The original single-bracket check treated
// "()" as strong noun-like context and incorrectly demoted valid numbers when
// "に" / "しかない" were split into two conversion segments.
TEST(ContextualCandidateRerankerTest,
     NiShikaSplitQuantityKeepsNumberAfterBracketOnlyContext) {
  for (absl::string_view left : {"()", "（）", "[]", "［］", "「」",
                                "(( ))", "（　）", "()  "}) {
    Segments segments;
    Segment* ni = AddConversionSegment(&segments, "に");
    AddCandidate(ni, "に", "二", 7959);
    AddCandidate(ni, "に", "に", 8419);
    Segment* shika = AddConversionSegment(&segments, "しかない");
    AddCandidate(shika, "しかない", "しかない", 4910);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, left);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二")
        << std::string(left);
  }
}

// Weak punctuation context must still preserve the previously fixed case
// particle when the negative predicate specifically signals "できない".
TEST(ContextualCandidateRerankerTest,
     NiShikaSplitFunctionalPredicateAfterBracketOnlyContext) {
  for (absl::string_view left : {"()", "（）", "[]", "「」", "( )"}) {
    Segments segments;
    Segment* ni = AddConversionSegment(&segments, "に");
    AddCandidate(ni, "に", "二", 7959);
    AddCandidate(ni, "に", "に", 8419);
    Segment* shika = AddConversionSegment(&segments, "しかできない");
    AddCandidate(shika, "しかできない", "しかできない", 4910);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, left);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に")
        << std::string(left);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiShikaNonemptyParenthesesStillCountAsLeftContext) {
  Segments segments;
  Segment* ni = AddConversionSegment(&segments, "に");
  AddCandidate(ni, "に", "二", 7959);
  AddCandidate(ni, "に", "に", 8419);
  Segment* shika = AddConversionSegment(&segments, "しかない");
  AddCandidate(shika, "しかない", "しかない", 4910);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments, "（管理者）");
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に");
}

// The dedicated ni-shika rule, not the generic particle rule, must handle a
// split "に" + "しかない".  These are valid numerical/topic constructions.
TEST(ContextualCandidateRerankerTest,
     NiShikaSplitQuantityAfterTopicHistoryKeepsNumber) {
  for (absl::string_view topic : {"候補は", "プランは", "候補も", "数量なら"}) {
    Segments segments;
    AddHistorySegment(&segments, topic, topic);
    Segment* ni = AddConversionSegment(&segments, "に");
    AddCandidate(ni, "に", "二", 7959);
    AddCandidate(ni, "に", "に", 8419);
    Segment* shika = AddConversionSegment(&segments, "しかない");
    AddCandidate(shika, "しかない", "しかない", 4910);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二")
        << std::string(topic);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiShikaSplitQuantityAfterClientTopicKeepsNumber) {
  for (absl::string_view topic : {"候補は", "プランは", "候補も", "数量なら"}) {
    Segments segments;
    Segment* ni = AddConversionSegment(&segments, "に");
    AddCandidate(ni, "に", "二", 7959);
    AddCandidate(ni, "に", "に", 8419);
    Segment* shika = AddConversionSegment(&segments, "しかない");
    AddCandidate(shika, "しかない", "しかない", 4910);
    ContextualCandidateReranker reranker;
    reranker.Rerank(&segments, topic);
    EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二")
        << std::string(topic);
  }
}

TEST(ContextualCandidateRerankerTest,
     NiShikaSplitFunctionalAfterNounHistoryStillPromotesParticle) {
  Segments segments;
  AddHistorySegment(&segments, "かんりしゃ", "管理者");
  Segment* ni = AddConversionSegment(&segments, "に");
  AddCandidate(ni, "に", "二", 7959);
  AddCandidate(ni, "に", "に", 8419);
  Segment* shika = AddConversionSegment(&segments, "しかできない");
  AddCandidate(shika, "しかできない", "しかできない", 4910);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に");
}

// The newest committed HISTORY segment must take precedence over older
// history.  A previous noun cannot cross a hard sentence boundary.
TEST(ContextualCandidateRerankerTest,
     GenericNiParticleDoesNotUseOlderHistoryAcrossSentenceBoundary) {
  Segments segments;
  AddHistorySegment(&segments, "かんりしゃ", "管理者");
  AddHistorySegment(&segments, "。", "。");
  Segment* ni = AddConversionSegment(&segments, "に");
  AddCandidate(ni, "に", "二", 7959);
  AddCandidate(ni, "に", "に", 8419);
  Segment* sono = AddConversionSegment(&segments, "その");
  AddCandidate(sono, "その", "その", 4910);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments, "API");
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二");
}

TEST(ContextualCandidateRerankerTest,
     GenericNiParticleDoesNotUseOlderHistoryAcrossTopicFrame) {
  Segments segments;
  AddHistorySegment(&segments, "えーぴーあい", "API");
  AddHistorySegment(&segments, "こうほは", "候補は");
  Segment* ni = AddConversionSegment(&segments, "に");
  AddCandidate(ni, "に", "二", 7959);
  AddCandidate(ni, "に", "に", 8419);
  Segment* sono = AddConversionSegment(&segments, "その");
  AddCandidate(sono, "その", "その", 4910);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "二");
}

TEST(ContextualCandidateRerankerTest,
     GenericNiParticleUsesNewestNounHistoryAfterOlderBoundary) {
  Segments segments;
  AddHistorySegment(&segments, "。", "。");
  AddHistorySegment(&segments, "かんりしゃ", "管理者");
  Segment* ni = AddConversionSegment(&segments, "に");
  AddCandidate(ni, "に", "二", 7959);
  AddCandidate(ni, "に", "に", 8419);
  Segment* sono = AddConversionSegment(&segments, "その");
  AddCandidate(sono, "その", "その", 4910);
  ContextualCandidateReranker reranker;
  reranker.Rerank(&segments);
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "に");
}

}  // namespace
}  // namespace engine
}  // namespace mozc
