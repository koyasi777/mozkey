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

#ifndef MOZC_CONVERTER_IMMUTABLE_CONVERTER_H_
#define MOZC_CONVERTER_IMMUTABLE_CONVERTER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "converter/connector.h"
#include "converter/immutable_converter_interface.h"
#include "converter/lattice.h"
#include "converter/nbest_generator.h"
#include "converter/node.h"
#include "converter/node_list_builder.h"
#include "converter/segmenter.h"
#include "converter/segments.h"
#include "dictionary/dictionary_interface.h"
#include "dictionary/pos_group.h"
#include "dictionary/pos_matcher.h"
#include "engine/modules.h"
#include "prediction/suggestion_filter.h"
#include "request/options.h"

namespace mozc {

class ImmutableConverter : public ImmutableConverterInterface {
 public:
  explicit ImmutableConverter(const engine::Modules& modules);
  ImmutableConverter(const ImmutableConverter&) = delete;
  ImmutableConverter& operator=(const ImmutableConverter&) = delete;
  ~ImmutableConverter() override = default;

  // Accepts the internal lattice structure for debugging.
  [[nodiscard]] bool Convert(const ConversionOptions& options,
                             Segments* segments, Lattice* lattice) const;

  [[nodiscard]] bool Convert(const ConversionOptions& options,
                             Segments* segments) const override;

 private:
  friend class ImmutableConverterTestPeer;

  enum InsertCandidatesType {
    MULTI_SEGMENTS,      // Normal conversion ("私の|名前は|中野です")
    SINGLE_SEGMENT,      // Realtime conversion ("私の名前は中野です")
    ONLY_FIRST_SEGMENT,  // Insert only first segment ("私の")
    // Insert only first segment for n-best path
    // ここでは着物を脱ぐ
    // ここで履物を脱ぐ
    // → ここでは, ここで
    FIRST_INNER_SEGMENT,
  };

  void InsertDummyCandidates(Segment* segment, size_t expand_size) const;
  std::vector<Node*> Lookup(int begin_pos, const ConversionOptions& options,
                            bool is_reverse, Lattice* lattice) const;
  void AddCharacterTypeBasedNodes(absl::string_view key_substr,
                                  Lattice* lattice,
                                  BaseNodeListBuilder* builder) const;

  void Resegment(const Segments& segments, absl::string_view history_key,
                 absl::string_view conversion_key, Lattice* lattice) const;

  void ApplyResegmentRules(size_t pos, Lattice* lattice) const;
  // Returns true resegmentation happened
  bool ResegmentArabicNumberAndSuffix(size_t pos, Lattice* lattice) const;
  bool ResegmentPrefixAndArabicNumber(size_t pos, Lattice* lattice) const;
  bool ResegmentPersonalName(size_t pos, Lattice* lattice) const;

  bool MakeLattice(const ConversionOptions& options, Segments* segments,
                   Lattice* lattice) const;
  bool MakeLatticeNodesForHistorySegments(const Segments& segments,
                                          const ConversionOptions& options,
                                          Lattice* lattice) const;
  void MakeLatticeNodesForConversionSegments(const Segments& segments,
                                             const ConversionOptions& options,
                                             absl::string_view history_key,
                                             Lattice* lattice) const;
  // Fixes for "好む" vs "この|無", "大|代" vs "代々" preferences.
  // If the last node ends with "prefix", give an extra
  // wcost penalty. In this case  "無" doesn't tend to appear at
  // user input.
  void ApplyPrefixSuffixPenalty(const ConversionOptions& options,
                                absl::string_view conversion_key,
                                Lattice* lattice) const;

  // Penalizes one-kana one-kanji nodes that steal a natural functional-kana
  // path such as "に/は" or "た/の".
  void ApplyFunctionalKanaGuard(absl::string_view history_key,
                                Lattice* lattice) const;

  // Applies a Viterbi-time soft prior around the history/conversion boundary
  // to reduce ranking drift caused by splitting an input sequence into
  // committed history and current conversion.  This scorer uses only evidence
  // already present in the current lattice: a functional-prefix or whole
  // functional-chain path, and a competing content node that consumes it.
  void ApplyContextualRecompositionScorer(absl::string_view history_key,
                                          Lattice* lattice) const;

  void ApplySahenShitaiGuard(absl::string_view history_key,
                             Lattice* lattice) const;

  // Suppresses "した..." -> content-word hijacks such as "下", "舌", and
  // "シタン" after a committed sahen noun.  This is intentionally separate
  // from the generic boundary scorer because these can be legitimate words in
  // other contexts, e.g. "机の下", "舌の痛み", or "紫檀".
  void ApplySahenShitaContentHijackGuard(absl::string_view history_key,
                                         Lattice* lattice) const;

  // Penalizes "りつ" content nodes such as "率" or "律" after administrative
  // history contexts where the suffix "立" is much more likely, as in:
  //   山梨県 + りつ + 美術館 -> 山梨県立美術館
  void ApplyAdministrativeRitsuGuard(absl::string_view history_key,
                                     Lattice* lattice) const;

  // Penalizes short "content + が" splits when the same span has a place-like
  // candidate followed by a strong place suffix, such as:
  //   し + が + 方面 -> 滋賀方面
  //   さ + が + 空港 -> 佐賀空港
  void ApplyPlaceSuffixGuard(absl::string_view history_key,
                             Lattice* lattice) const;

  // Penalizes content nodes that steal a natural adverbial "Xに + は/も/..."
  // phrase, such as:
  //
  //   とく | には -> 得 | には
  //
  // when a same-start adverbial candidate such as:
  //
  //   とくに -> 特に
  //
  // exists in the lattice.  This keeps generated daily entries from breaking
  // high-frequency grammar-like phrases while still keeping those candidates
  // available.
  void ApplyAdverbialNiGuard(absl::string_view history_key,
                             Lattice* lattice) const;

  // Penalizes full-span dictionary proper nouns whose readings look like
  // ordinary inflected Japanese phrases.
  //
  // Note: generated daily/nico-pixiv entries may be emitted as 名詞,一般 rather
  // than unique nouns.  Those entries are primarily controlled by dictionary
  // generation cost policy.  This guard protects runtime lattice cases where
  // the node is actually tagged as a unique noun.
  void ApplyGrammarLikeProperNounGuard(absl::string_view history_key,
                                       Lattice* lattice) const;

  // Penalizes fragile adjacent short content splits such as:
  //
  //   に | じ -> 二 | 時
  //
  // when the same two-kana span has a competitive whole-span candidate such as:
  //
  //   にじ -> 虹 / 二字 / 二時
  //
  // This guard is intentionally narrow.  It does not rewrite candidates and it
  // does not special-case any word.  It only adjusts lattice costs before
  // Viterbi so that an overly short split is not selected too eagerly.
  void ApplyShortCompoundSplitGuard(absl::string_view history_key,
                                    Lattice* lattice) const;

  bool Viterbi(const ConversionOptions& options, const Segments& segments,
               Lattice* lattice) const;

  bool PredictionViterbi(const ConversionOptions& options,
                         const Segments& segments, Lattice* lattice) const;
  void PredictionViterbiInternal(const ConversionOptions& options,
                                 int calc_begin_pos, int calc_end_pos,
                                 Lattice* lattice) const;

  // TODO(toshiyuki): Change parameter order for mutable |segments|.

  // Inserts first segment from conversion result to candidates.
  // Costs will be modified using the existing candidates.
  void InsertFirstSegmentToCandidates(const ConversionOptions& options,
                                      Segments* segments,
                                      const Lattice& lattice,
                                      absl::Span<const uint16_t> group,
                                      size_t max_candidates_size,
                                      bool allow_exact) const;

  void InsertCandidates(const ConversionOptions& options, Segments* segments,
                        const Lattice& lattice,
                        absl::Span<const uint16_t> group,
                        size_t max_candidates_size,
                        InsertCandidatesType type) const;

  template <typename TConnector>
  void InsertCandidatesImpl(TConnector& conn, const ConversionOptions& options,
                            Segments* segments, const Lattice& lattice,
                            absl::Span<const uint16_t> group,
                            size_t max_candidates_size,
                            InsertCandidatesType type) const;

  void InsertCandidatesForRealtimeWithCandidateChecker(
      const ConversionOptions& options, const Lattice& lattice,
      absl::Span<const uint16_t> group, Segments* segments) const;

  // Helper function for InsertCandidates().
  // Returns true if |node| is valid node for segment end.
  bool IsSegmentEndNode(const ConversionOptions& options,
                        const Segments& segments, const Node* node,
                        absl::Span<const uint16_t> group,
                        bool is_single_segment) const;

  // Helper function for InsertCandidates().
  // Returns the segment for inserting candidates.
  Segment* GetInsertTargetSegment(const Lattice& lattice,
                                  absl::Span<const uint16_t> group,
                                  InsertCandidatesType type, size_t begin_pos,
                                  const Node* node, Segments* segments) const;

  bool MakeSegments(const ConversionOptions& options, const Lattice& lattice,
                    Segments* segments) const;

  std::vector<uint16_t> MakeGroup(const Segments& segments) const;

  inline int GetCost(const Node* lnode, const Node* rnode) const {
    const int kInvalidPenaltyCost = 100000;
    if (rnode->constrained_prev != nullptr &&
        lnode != rnode->constrained_prev) {
      return kInvalidPenaltyCost;
    }
    return connector_.GetTransitionCost(lnode->rid, rnode->lid) + rnode->wcost;
  }

  void InsertCandidatesForConversion(const ConversionOptions& options,
                                     const Lattice& lattice,
                                     absl::Span<const uint16_t> group,
                                     Segments* segments) const;

  void InsertCandidatesForPrediction(const ConversionOptions& options,
                                     const Lattice& lattice,
                                     absl::Span<const uint16_t> group,
                                     Segments* segments) const;

  const dictionary::DictionaryInterface& dictionary_;
  const dictionary::DictionaryInterface& suffix_dictionary_;
  const dictionary::UserDictionaryInterface& user_dictionary_;
  const Connector& connector_;
  const Segmenter& segmenter_;
  const dictionary::PosMatcher& pos_matcher_;
  const dictionary::PosGroup& pos_group_;
  const SuggestionFilter& suggestion_filter_;

  // Cache for POS ids.
  const uint16_t first_name_id_;
  const uint16_t last_name_id_;
  const uint16_t number_id_;
  const uint16_t unknown_id_;

  // Cache for transition cost.
  const int32_t last_to_first_name_transition_cost_;
};

}  // namespace mozc

#endif  // MOZC_CONVERTER_IMMUTABLE_CONVERTER_H_
