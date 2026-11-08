// src/engine/contextual_candidate_reranker.cc

#include "engine/contextual_candidate_reranker.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/util.h"
#include "converter/candidate.h"
#include "converter/segments.h"

namespace mozc {
namespace engine {
namespace {

using ::mozc::converter::Candidate;
using ::mozc::converter::Segment;
using ::mozc::converter::Segments;

bool ContainsScriptType(absl::string_view text, Util::ScriptType type) {
  ConstChar32Iterator iter(text);
  for (; !iter.Done(); iter.Next()) {
    if (Util::GetScriptType(iter.Get()) == type) {
      return true;
    }
  }
  return false;
}

bool ContainsKanji(absl::string_view text) {
  return ContainsScriptType(text, Util::KANJI);
}

bool ContainsKatakana(absl::string_view text) {
  return ContainsScriptType(text, Util::KATAKANA);
}

bool ContainsNumber(absl::string_view text) {
  return ContainsScriptType(text, Util::NUMBER);
}

bool IsPureHiragana(absl::string_view text) {
  if (text.empty()) {
    return false;
  }

  ConstChar32Iterator iter(text);
  for (; !iter.Done(); iter.Next()) {
    if (Util::GetScriptType(iter.Get()) != Util::HIRAGANA) {
      return false;
    }
  }
  return true;
}

bool IsAsciiOrSymbolLike(absl::string_view text) {
  if (text.empty()) {
    return false;
  }

  ConstChar32Iterator iter(text);
  for (; !iter.Done(); iter.Next()) {
    const char32_t c = iter.Get();
    const Util::ScriptType type = Util::GetScriptType(c);
    if (type == Util::ALPHABET || type == Util::NUMBER) {
      continue;
    }

    switch (c) {
      case U'-':
      case U'_':
      case U'#':
      case U'/':
      case U'.':
        continue;
      default:
        return false;
    }
  }

  return true;
}

bool StartsWith(absl::string_view text, absl::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(absl::string_view text, absl::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

bool IsParticleReading(absl::string_view key) {
  return key == "に" || key == "を" || key == "が" || key == "は" ||
         key == "へ" || key == "で" || key == "と" || key == "の" ||
         key == "も";
}

bool IsFunctionalParticleExpressionKey(absl::string_view key) {
  // Compound functional particles that should usually stay in kana after
  // noun-like or ASCII-like context:
  //   githubには
  //   mainにも
  //   東京では
  //   PRとは
  //
  // Keep this list conservative. Longer expressions such as "について" and
  // "として" have wider contextual behavior and should be handled separately.
  return key == "には" || key == "にも" || key == "では" ||
         key == "でも" || key == "とは";
}

bool IsPlainParticleSurfaceCandidate(const Segment& segment,
                                     const Candidate& candidate) {
  if (!IsParticleReading(segment.key())) {
    return false;
  }

  // Some duplicate candidates may have different POS IDs or internal costs.
  // For contextual particle reranking, the surface form is what matters.
  return candidate.value == segment.key() &&
         candidate.content_value == segment.key();
}

bool IsSuspiciousHomophoneForParticle(const Segment& segment,
                                      const Candidate& candidate) {
  if (!IsParticleReading(segment.key())) {
    return false;
  }

  // In a particle reading segment, anything whose surface is not the plain
  // particle is suspicious in ordinary left-context prose:
  //   に゛, ニ, ni, 二, 弐, 2, Ⅱ, 似, 荷, ...
  //
  // Do not try to classify by script type here. "に゛" can look hiragana-like
  // depending on Unicode/script classification, but it must not beat "に".
  return !IsPlainParticleSurfaceCandidate(segment, candidate);
}

bool IsFunctionalParticleExpressionCandidate(const Segment& segment,
                                             const Candidate& candidate) {
  if (!IsFunctionalParticleExpressionKey(segment.key())) {
    return false;
  }

  // Surface form is what matters here. Some duplicate candidates may differ
  // in POS IDs or internal costs, but "には" should beat "二は", "2は",
  // "弐は", etc. after noun-like context.
  return candidate.value == segment.key() &&
         candidate.content_value == segment.key();
}

bool IsSuspiciousCandidateForFunctionalParticleExpression(
    const Segment& segment, const Candidate& candidate) {
  if (!IsFunctionalParticleExpressionKey(segment.key())) {
    return false;
  }

  if (IsFunctionalParticleExpressionCandidate(segment, candidate)) {
    return false;
  }

  // In a compound functional-particle segment, anything other than the plain
  // kana expression is suspicious after noun-like context:
  //   には -> 二は, 弐は, 2は, 似は, 荷は
  //   にも -> 二も, 弐も, 2も
  //   では -> 出は, デハ
  //   とは -> 戸は
  return true;
}

bool HasHistorySegment(const Segments& segments) {
  return segments.segments_size() > 0 &&
         segments.segment(0).segment_type() == Segment::HISTORY &&
         segments.segment(0).candidates_size() > 0;
}

bool PreviousContextLooksNounLike(const Segments& segments,
                                  size_t conversion_index) {
  if (conversion_index == 0) {
    // A newly committed sentence may leave several HISTORY segments.  The
    // oldest one is not evidence for the particle immediately after the newest.
    const size_t count = segments.history_segments_size();
    if (count == 0) {
      return false;
    }
    const Segment& history = segments.history_segment(count - 1);
    if (history.candidates_size() == 0) {
      return false;
    }
    absl::string_view value = history.candidate(0).value;
    while (EndsWith(value, " ") || EndsWith(value, "\t") ||
           EndsWith(value, "　")) {
      value.remove_suffix(EndsWith(value, "　") ?
                          absl::string_view("　").size() : 1);
    }
    if (value.empty()) {
      return false;
    }
    // A sentence boundary or quantity/topic frame is not noun-like evidence.
    // In particular, do not turn "候補は二しかない" into "候補はにしかない".
    for (absl::string_view ending : {"。", "！", "!", "？", "?",
                                    "\n", "\r", ";", "；",
                                    "は", "も", "なら"}) {
      if (EndsWith(value, ending)) {
        return false;
      }
    }
    return true;
  }

  const Segment& previous = segments.conversion_segment(conversion_index - 1);
  if (previous.candidates_size() == 0) {
    return false;
  }

  const Candidate& previous_top = previous.candidate(0);
  if (previous_top.value.empty()) {
    return false;
  }

  // Coarse heuristic. This intentionally does not try to be a full POS parser.
  return ContainsKanji(previous_top.value) ||
         ContainsKatakana(previous_top.value) ||
         IsAsciiOrSymbolLike(previous_top.value);
}

bool NextContextLooksPhraseContinuation(const Segments& segments,
                                        size_t conversion_index) {
  if (conversion_index + 1 >= segments.conversion_segments_size()) {
    // Even without right context, noun + particle is often valid.
    // But for ambiguous particles such as "の" and "と", right context is
    // desirable. Keep this true for now; per-particle strength controls risk.
    return true;
  }

  const Segment& next = segments.conversion_segment(conversion_index + 1);
  if (next.candidates_size() == 0) {
    return false;
  }

  const Candidate& top = next.candidate(0);

  if (top.value.empty()) {
    return false;
  }

  if (ContainsKatakana(top.value)) {
    return true;
  }

  if (ContainsKanji(top.value)) {
    return true;
  }

  // Verb/adjective continuations are often represented partly in hiragana:
  //   に 送る
  //   を する
  //   が ある
  // Avoid being too strict here.
  if (IsPureHiragana(top.value)) {
    return true;
  }

  return false;
}

bool IsCounterLikeValue(absl::string_view value) {
  if (value.empty()) {
    return false;
  }

  // Counters and noun-like quantifier endings.  This intentionally stays
  // conservative; it is only used for protecting "しか + negative" after
  // quantity-like context.
  return EndsWith(value, "名") || EndsWith(value, "人") ||
         EndsWith(value, "者") || EndsWith(value, "件") ||
         EndsWith(value, "個") || EndsWith(value, "つ") ||
         EndsWith(value, "回") || EndsWith(value, "社") ||
         EndsWith(value, "点") || EndsWith(value, "円") ||
         EndsWith(value, "年") || EndsWith(value, "月") ||
         EndsWith(value, "日") || EndsWith(value, "時") ||
         EndsWith(value, "分") || EndsWith(value, "秒") ||
         EndsWith(value, "本") || EndsWith(value, "枚") ||
         EndsWith(value, "台") || EndsWith(value, "冊") ||
         EndsWith(value, "通") || EndsWith(value, "行") ||
         EndsWith(value, "匹") || EndsWith(value, "頭") ||
         EndsWith(value, "羽");
}

bool IsShikaIruPrefixKey(absl::string_view key) {
  // Ambiguous live-conversion prefix:
  //   しかい...
  //
  // This collides heavily with:
  //   司会
  //   視界
  //   歯科医
  //
  // Therefore, after an attributive "の" context, do not force hiragana.
  return StartsWith(key, "しかい");
}

bool IsShikaExplicitNegativePrefixKey(absl::string_view key) {
  // Less ambiguous negative-expression prefixes:
  //   しかない
  //   しかありません
  //   しかおらない
  //   しかおりません
  return StartsWith(key, "しかな") || StartsWith(key, "しかあり") ||
         StartsWith(key, "しかお");
}

bool PreviousValueLooksShikaCompatible(absl::string_view value,
                                       absl::string_view current_key) {
  if (value.empty()) {
    return false;
  }

  // Do not rewrite ordinary attributive "の + しかい..." contexts:
  //   次のしかい    -> 次の司会
  //   会議のしかい  -> 会議の司会
  //   山田のしかい  -> 山田の司会
  //
  // But allow explicit negative expressions:
  //   山田のしかない
  //   これのしかない
  //
  // The key distinction is that "しかい..." is still highly ambiguous with
  // "司会/視界/歯科医", while "しかな..." and "しかあり..." already express
  // the negative construction.
  if (EndsWith(value, "の") && IsShikaIruPrefixKey(current_key)) {
    return false;
  }

  if (ContainsNumber(value) || IsCounterLikeValue(value)) {
    return true;
  }

  // Noun-like context:
  //   山田しかいない
  //   東京しかない
  //   mainしかない
  return ContainsKanji(value) || ContainsKatakana(value) ||
         IsAsciiOrSymbolLike(value);
}

bool PreviousContextLooksShikaCompatible(const Segments& segments,
                                         size_t conversion_index,
                                         absl::string_view current_key) {
  if (conversion_index == 0) {
    if (!HasHistorySegment(segments)) {
      return false;
    }

    const Segment& history = segments.segment(0);
    const Candidate& candidate = history.candidate(0);
    return PreviousValueLooksShikaCompatible(candidate.value, current_key);
  }

  const Segment& previous = segments.conversion_segment(conversion_index - 1);
  if (previous.candidates_size() == 0) {
    return false;
  }

  const Candidate& previous_top = previous.candidate(0);
  return PreviousValueLooksShikaCompatible(previous_top.value, current_key);
}

bool IsShikaParticleLikeKey(absl::string_view key) {
  return key == "しか" || EndsWith(key, "しか");
}

bool IsShikaNegativePrefixKey(absl::string_view key) {
  // Protect live-conversion prefixes of:
  //   しかいない / しかいません
  //   しかない
  //   しかありません
  //   しかおらない / しかおりません
  return IsShikaIruPrefixKey(key) || IsShikaExplicitNegativePrefixKey(key);
}

bool IsShikaFunctionalCandidate(const Segment& segment,
                                const Candidate& candidate) {
  const absl::string_view key = segment.key();

  if (IsShikaParticleLikeKey(key)) {
    return EndsWith(candidate.value, "しか") &&
           EndsWith(candidate.content_value, "しか");
  }

  if (IsShikaNegativePrefixKey(key)) {
    return StartsWith(candidate.value, "しか") &&
           StartsWith(candidate.content_value, "しか") &&
           IsPureHiragana(candidate.value);
  }

  return false;
}

bool IsSuspiciousCandidateForShikaNegative(const Segment& segment,
                                           const Candidate& candidate) {
  if (IsShikaFunctionalCandidate(segment, candidate)) {
    return false;
  }

  const absl::string_view key = segment.key();

  if (IsShikaParticleLikeKey(key)) {
    // Examples to demote after quantity-like context:
    //   名歯科, 名士か, 名師か, 名史か
    return ContainsKanji(candidate.value) || ContainsKatakana(candidate.value) ||
           ContainsNumber(candidate.value) || !IsPureHiragana(candidate.value);
  }

  if (IsShikaNegativePrefixKey(key)) {
    // Examples to demote after quantity-like context:
    //   司会, 視界, 歯科医, 視界内, 士会内
    return ContainsKanji(candidate.value) || ContainsKatakana(candidate.value) ||
           ContainsNumber(candidate.value) || !IsPureHiragana(candidate.value);
  }

  return false;
}

int ShikaNegativeBonus(absl::string_view key) {
  if (IsShikaNegativePrefixKey(key)) {
    return 7000;
  }

  if (IsShikaParticleLikeKey(key)) {
    return 5000;
  }

  return 0;
}

int ParticleBonus(absl::string_view key) {
  if (key == "に" || key == "を" || key == "が" || key == "へ" ||
      key == "で") {
    return 5000;
  }

  if (key == "は" || key == "も") {
    return 4000;
  }

  if (key == "と" || key == "の") {
    return 2500;
  }

  return 0;
}

int FunctionalParticleExpressionBonus(absl::string_view key) {
  if (key == "には" || key == "にも") {
    return 5000;
  }

  if (key == "では" || key == "でも") {
    return 4000;
  }

  if (key == "とは") {
    return 3500;
  }

  return 0;
}

struct CandidateScore {
  int index = 0;
  int adjusted_cost = 0;
};

// A bare digit ("二") is normally followed by a counter, not by a
// demonstrative determiner ("その内容", "この文書", etc.).  Treat this as a
// right-hand grammatical signal only for a standalone "に" segment.
// This does not affect "にしか" or number + counter expressions.
bool NextSegmentIsDemonstrativeModifier(const Segments& segments,
                                        size_t conversion_index) {
  if (conversion_index + 1 >= segments.conversion_segments_size()) {
    return false;
  }
  const Segment& next = segments.conversion_segment(conversion_index + 1);
  if (next.candidates_size() == 0) {
    return false;
  }
  for (absl::string_view word : {"この", "その", "あの", "どの",
                                 "こんな", "そんな", "あんな", "どんな"}) {
    if (next.key() == word && next.candidate(0).value == word) {
      return true;
    }
  }
  return false;
}

// Do not infer a particle merely from whitespace, a new sentence, or a
// quantity-topic frame.  The text is used transiently and never retained.
bool HasExternalNiParticleLeftContext(absl::string_view preceding_text) {
  while (EndsWith(preceding_text, " ") || EndsWith(preceding_text, "\t") ||
         EndsWith(preceding_text, "　")) {
    preceding_text.remove_suffix(EndsWith(preceding_text, "　") ?
                                 absl::string_view("　").size() : 1);
  }
  if (preceding_text.empty()) {
    return false;
  }
  for (absl::string_view ending : {"。", "！", "!", "？", "?",
                                  "\n", "\r", ";", "；",
                                  "は", "も", "なら"}) {
    if (EndsWith(preceding_text, ending)) {
      return false;
    }
  }
  return true;
}

void RerankParticleSegmentAfterContext(const Segments& segments,
                                       size_t conversion_index,
                                       Segment* segment,
                                       absl::string_view preceding_text) {
  if (segment == nullptr) {
    return;
  }

  if (!IsParticleReading(segment->key())) {
    return;
  }

  if (segment->candidates_size() <= 1) {
    return;
  }

  const bool has_internal_context =
      PreviousContextLooksNounLike(segments, conversion_index);
  // A committed left context may exist only in commands::Context: converter
  // history is not guaranteed to contain previously committed punctuation.
  // Limit this fallback to "に" followed by demonstrative modification so
  // it cannot override legitimate numbers followed by counters/units.
  const bool has_external_context =
      !has_internal_context && conversion_index == 0 &&
      segments.history_segments_size() == 0 && segment->key() == "に" &&
      HasExternalNiParticleLeftContext(preceding_text) &&
      NextSegmentIsDemonstrativeModifier(segments, conversion_index);
  if (!has_internal_context && !has_external_context) {
    return;
  }

  if (!NextContextLooksPhraseContinuation(segments, conversion_index)) {
    return;
  }

  int plain_particle_index = -1;
  for (int i = 0; i < segment->candidates_size(); ++i) {
    if (IsPlainParticleSurfaceCandidate(*segment, segment->candidate(i))) {
      plain_particle_index = i;
      break;
    }
  }

  if (plain_particle_index < 0) {
    return;
  }

  std::vector<CandidateScore> scores;
  scores.reserve(segment->candidates_size());

  // The external-context signal is weaker than an actual Mozc history
  // segment.  The real-dictionary numeric/kana gap here is 460 cost points.
  const int bonus = has_external_context ? 1000 : ParticleBonus(segment->key());

  for (int i = 0; i < segment->candidates_size(); ++i) {
    const Candidate& candidate = segment->candidate(i);

    int adjusted_cost = candidate.cost;

    if (IsPlainParticleSurfaceCandidate(*segment, candidate)) {
      adjusted_cost -= bonus;
    } else if (IsSuspiciousHomophoneForParticle(*segment, candidate)) {
      adjusted_cost += bonus;
    }

    scores.push_back({i, adjusted_cost});
  }

  std::stable_sort(scores.begin(), scores.end(),
                   [](const CandidateScore& lhs, const CandidateScore& rhs) {
                     return lhs.adjusted_cost < rhs.adjusted_cost;
                   });

  // If the top candidate does not change, avoid unnecessary mutation.
  if (scores.empty() || scores[0].index == 0) {
    return;
  }

  std::vector<Candidate> reordered;
  reordered.reserve(segment->candidates_size());
  for (const CandidateScore& score : scores) {
    reordered.push_back(segment->candidate(score.index));
  }

  for (int i = 0; i < static_cast<int>(reordered.size()); ++i) {
    *segment->mutable_candidate(i) = reordered[i];
  }
}

void RerankFunctionalParticleExpressionAfterContext(
    const Segments& segments, size_t conversion_index, Segment* segment) {
  if (segment == nullptr) {
    return;
  }

  if (!IsFunctionalParticleExpressionKey(segment->key())) {
    return;
  }

  if (segment->candidates_size() <= 1) {
    return;
  }

  if (!PreviousContextLooksNounLike(segments, conversion_index)) {
    return;
  }

  int functional_candidate_index = -1;
  for (int i = 0; i < segment->candidates_size(); ++i) {
    if (IsFunctionalParticleExpressionCandidate(*segment,
                                                segment->candidate(i))) {
      functional_candidate_index = i;
      break;
    }
  }

  if (functional_candidate_index < 0) {
    return;
  }

  std::vector<CandidateScore> scores;
  scores.reserve(segment->candidates_size());

  const int bonus = FunctionalParticleExpressionBonus(segment->key());

  for (int i = 0; i < segment->candidates_size(); ++i) {
    const Candidate& candidate = segment->candidate(i);

    int adjusted_cost = candidate.cost;

    if (IsFunctionalParticleExpressionCandidate(*segment, candidate)) {
      adjusted_cost -= bonus;
    } else if (IsSuspiciousCandidateForFunctionalParticleExpression(
                   *segment, candidate)) {
      adjusted_cost += bonus;
    }

    scores.push_back({i, adjusted_cost});
  }

  std::stable_sort(scores.begin(), scores.end(),
                   [](const CandidateScore& lhs, const CandidateScore& rhs) {
                     return lhs.adjusted_cost < rhs.adjusted_cost;
                   });

  // If the top candidate does not change, avoid unnecessary mutation.
  if (scores.empty() || scores[0].index == 0) {
    return;
  }

  std::vector<Candidate> reordered;
  reordered.reserve(segment->candidates_size());
  for (const CandidateScore& score : scores) {
    reordered.push_back(segment->candidate(score.index));
  }

  for (int i = 0; i < static_cast<int>(reordered.size()); ++i) {
    *segment->mutable_candidate(i) = reordered[i];
  }
}

void RerankShikaNegativeExpressionAfterContext(const Segments& segments,
                                               size_t conversion_index,
                                               Segment* segment) {
  if (segment == nullptr) {
    return;
  }

  if (segment->candidates_size() <= 1) {
    return;
  }

  if (!IsShikaParticleLikeKey(segment->key()) &&
      !IsShikaNegativePrefixKey(segment->key())) {
    return;
  }

  if (!PreviousContextLooksShikaCompatible(segments, conversion_index,
                                           segment->key())) {
    return;
  }

  int functional_candidate_index = -1;
  for (int i = 0; i < segment->candidates_size(); ++i) {
    if (IsShikaFunctionalCandidate(*segment, segment->candidate(i))) {
      functional_candidate_index = i;
      break;
    }
  }

  if (functional_candidate_index < 0) {
    return;
  }

  std::vector<CandidateScore> scores;
  scores.reserve(segment->candidates_size());

  const int bonus = ShikaNegativeBonus(segment->key());

  for (int i = 0; i < segment->candidates_size(); ++i) {
    const Candidate& candidate = segment->candidate(i);

    int adjusted_cost = candidate.cost;

    if (IsShikaFunctionalCandidate(*segment, candidate)) {
      adjusted_cost -= bonus;
    } else if (IsSuspiciousCandidateForShikaNegative(*segment, candidate)) {
      adjusted_cost += bonus;
    }

    scores.push_back({i, adjusted_cost});
  }

  std::stable_sort(scores.begin(), scores.end(),
                   [](const CandidateScore& lhs, const CandidateScore& rhs) {
                     return lhs.adjusted_cost < rhs.adjusted_cost;
                   });

  // If the top candidate does not change, avoid unnecessary mutation.
  if (scores.empty() || scores[0].index == 0) {
    return;
  }

  std::vector<Candidate> reordered;
  reordered.reserve(segment->candidates_size());
  for (const CandidateScore& score : scores) {
    reordered.push_back(segment->candidate(score.index));
  }

  for (int i = 0; i < static_cast<int>(reordered.size()); ++i) {
    *segment->mutable_candidate(i) = reordered[i];
  }
}

// Match a functional "に + しか" reading rather than an arbitrary word
// beginning with those characters (e.g. にしかわ -> 西川).  For live conversion
// allow only prefixes of a small set of negative predicates.  A two-kana
// minimum for partial predicates excludes ambiguous single-kana suffixes.
bool IsNiShikaFunctionalContinuation(absl::string_view tail) {
  if (tail.empty() || tail == "で") {
    return true;
  }
  if (tail.size() < absl::string_view("ない").size()) {
    return false;
  }
  for (absl::string_view predicate : {
           "できない", "できません", "できず", "できなく", "できなかった",
           "できなければ", "いない", "いません", "いなかった",
           "ない", "なく", "なかった", "ありません", "ございません",
           "おりません", "おらず"}) {
    if (StartsWith(predicate, tail) || StartsWith(tail, predicate)) {
      return true;
    }
  }
  return false;
}

bool BeginsWithNiShikaReading(const Segments& segments, size_t conversion_index,
                             const Segment& segment,
                             absl::string_view* predicate_tail) {
  absl::string_view tail;
  if (StartsWith(segment.key(), "にしか")) {
    tail = segment.key().substr(absl::string_view("にしか").size());
  } else if (segment.key() == "に" &&
             conversion_index + 1 < segments.conversion_segments_size() &&
             StartsWith(segments.conversion_segment(conversion_index + 1).key(),
                        "しか")) {
    tail = segments.conversion_segment(conversion_index + 1).key().substr(
        absl::string_view("しか").size());
  } else {
    return false;
  }
  if (!IsNiShikaFunctionalContinuation(tail)) {
    return false;
  }
  *predicate_tail = tail;
  return true;
}

// A sequence of brackets, possibly separated by spaces, is punctuation,
// not lexical evidence that "に" is a case particle.  Looking only for a
// single-character bracket incorrectly treats "()" and "（）" as noun-like.
bool IsBracketOnlyContext(absl::string_view text) {
  ConstChar32Iterator iter(text);
  for (; !iter.Done(); iter.Next()) {
    switch (iter.Get()) {
      case U'(':
      case U')':
      case U'[':
      case U']':
      case U'{':
      case U'}':
      case U'（':
      case U'）':
      case U'［':
      case U'］':
      case U'｛':
      case U'｝':
      case U'「':
      case U'」':
      case U'『':
      case U'』':
      case U'【':
      case U'】':
      case U'〈':
      case U'〉':
      case U'《':
      case U'》':
      case U'"':
      case U'\'':
      case U' ':
      case U'\t':
      case U'　':
        break;
      default:
        return false;
    }
  }
  return true;
}

// The left context is a ranking hint, not proof that "に" is a particle.
// The nearest history segment takes precedence over older history and over the
// client's optional preceding_text.  Hard boundaries and quantity-topic
// markers are insufficient evidence for a particle here.
bool HasNiShikaLeftContext(const Segments& segments,
                          size_t conversion_index,
                          absl::string_view preceding_text,
                          absl::string_view predicate_tail) {
  absl::string_view value;
  if (conversion_index == 0) {
    const size_t history_size = segments.history_segments_size();
    if (history_size > 0) {
      const Segment& last_history = segments.history_segment(history_size - 1);
      if (last_history.candidates_size() > 0) {
        value = last_history.candidate(0).value;
      }
    }
  } else {
    const Segment& previous =
        segments.conversion_segment(conversion_index - 1);
    if (previous.candidates_size() > 0) {
      value = previous.candidate(0).value;
    }
  }
  if (value.empty()) {
    value = preceding_text;
  }
  if (value.empty()) {
    return false;
  }

  absl::string_view trimmed = value;
  while (EndsWith(trimmed, " ") || EndsWith(trimmed, "\t") ||
         EndsWith(trimmed, "　")) {
    trimmed.remove_suffix(EndsWith(trimmed, "　") ?
                          absl::string_view("　").size() : 1);
  }
  for (absl::string_view ending : {"。", "！", "!", "？", "?",
                                  "\n", "\r", ";", "；"}) {
    if (EndsWith(trimmed, ending)) {
      return false;
    }
  }
  // "プランは二しかない" is a legitimate quantity statement.  In such a
  // topic frame prefer the dictionary's existing number/particle ranking.
  if (EndsWith(trimmed, "は") || EndsWith(trimmed, "も") ||
      EndsWith(trimmed, "なら")) {
    return false;
  }
  // Brackets-only context, including paired/nested delimiters, is weak.
  // Keep the established live "にしかできない" correction, but do not force
  // ambiguous "二しかない" into kana solely because a bracket was committed.
  if (IsBracketOnlyContext(trimmed)) {
    return predicate_tail.empty() || StartsWith(predicate_tail, "でき") ||
           StartsWith(predicate_tail, "いな");
  }
  return true;
}

bool IsPlainNiShikaCandidate(const Segment& segment,
                             const Candidate& candidate) {
  // content_value is not equivalent to value. A correct functional expression
  // may have content_value without a particle, or may have it empty.
  return segment.key() == "に" ? candidate.value == "に"
                               : StartsWith(candidate.value, "にしか");
}

bool IsNumericNiShikaCandidate(const Segment& segment,
                               const Candidate& candidate) {
  if (segment.key() == "に") {
    return candidate.value == "二" || candidate.value == "2" ||
           candidate.value == "弐" || candidate.value == "Ⅱ";
  }
  return StartsWith(candidate.value, "二しか") ||
         StartsWith(candidate.value, "2しか") ||
         StartsWith(candidate.value, "弐しか") ||
         StartsWith(candidate.value, "Ⅱしか");
}

void RerankNiShikaAfterContext(const Segments& segments,
                               size_t conversion_index, Segment* segment,
                               absl::string_view preceding_text) {
  if (segment == nullptr || segment->candidates_size() <= 1) {
    return;
  }
  absl::string_view predicate_tail;
  if (!BeginsWithNiShikaReading(segments, conversion_index, *segment,
                               &predicate_tail) ||
      !HasNiShikaLeftContext(segments, conversion_index, preceding_text,
                             predicate_tail)) {
    return;
  }

  bool has_plain = false;
  bool has_number = false;
  for (int i = 0; i < segment->candidates_size(); ++i) {
    const Candidate& candidate = segment->candidate(i);
    has_plain |= IsPlainNiShikaCandidate(*segment, candidate);
    has_number |= IsNumericNiShikaCandidate(*segment, candidate);
  }
  if (!has_plain || !has_number) {
    return;
  }

  // Keep the preference narrower than the original +/-5000 (10000 total).
  constexpr int kKanaBonus = 3500;
  constexpr int kNumberPenalty = 1500;
  std::vector<CandidateScore> scores;
  scores.reserve(segment->candidates_size());
  for (int i = 0; i < segment->candidates_size(); ++i) {
    const Candidate& candidate = segment->candidate(i);
    int adjusted_cost = candidate.cost;
    if (IsPlainNiShikaCandidate(*segment, candidate)) {
      adjusted_cost -= kKanaBonus;
    } else if (IsNumericNiShikaCandidate(*segment, candidate)) {
      adjusted_cost += kNumberPenalty;
    }
    scores.push_back({i, adjusted_cost});
  }
  std::stable_sort(scores.begin(), scores.end(),
                   [](const CandidateScore& a, const CandidateScore& b) {
                     return a.adjusted_cost < b.adjusted_cost;
                   });
  if (scores.empty() || scores[0].index == 0) {
    return;
  }
  std::vector<Candidate> reordered;
  reordered.reserve(segment->candidates_size());
  for (const CandidateScore& score : scores) {
    reordered.push_back(segment->candidate(score.index));
  }
  for (int i = 0; i < static_cast<int>(reordered.size()); ++i) {
    *segment->mutable_candidate(i) = reordered[i];
  }
}

}  // namespace

void ContextualCandidateReranker::Rerank(
    Segments* segments, absl::string_view preceding_text) const {
  if (segments == nullptr) {
    return;
  }

  for (size_t i = 0; i < segments->conversion_segments_size(); ++i) {
    Segment* segment = segments->mutable_conversion_segment(i);
    absl::string_view ni_shika_tail;
    // A split "に" + "しかない/しかできない" is a single ambiguous functional
    // construction.  The dedicated rule must own its ranking decision; the
    // generic +/-5000 particle rule would otherwise promote "に" even after
    // quantity-topic history such as "候補は", before the dedicated rule can
    // decline to act.  Other "に" cases keep the existing generic rule.
    const bool is_split_ni_shika =
        segment->key() == "に" &&
        BeginsWithNiShikaReading(*segments, i, *segment, &ni_shika_tail);
    if (!is_split_ni_shika) {
      RerankParticleSegmentAfterContext(*segments, i, segment, preceding_text);
    }
    RerankFunctionalParticleExpressionAfterContext(*segments, i, segment);
    RerankShikaNegativeExpressionAfterContext(*segments, i, segment);
    RerankNiShikaAfterContext(*segments, i, segment, preceding_text);
  }
}

}  // namespace engine
}  // namespace mozc
