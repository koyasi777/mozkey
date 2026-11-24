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
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/util.h"
#include "composer/composer.h"
#include "composer/english_input_exceptions.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"
#include "transliteration/transliteration.h"

namespace mozc {
namespace {

// Full raw-token recognition is always exact. Candidate-only entries may
// preserve ordinary kana preedit; AUTO_ASCII requires the visible English form.
const composer::EnglishInputExceptions::Entry* ExceptionForRequest(
    const ConversionRequest& request) {
  if (!request.config().enable_english_input_exceptions() ||
      request.config().preedit_method() != config::Config::ROMAN ||
      request.request().special_romanji_table() !=
          commands::Request::DEFAULT_TABLE) {
    return nullptr;
  }
  const composer::ComposerData& composer = request.composer();
  if (composer.suppress_language_aware_raw_input() ||
      composer.GetInputMode() != transliteration::HIRAGANA ||
      composer.GetInputFieldType() == commands::Context::PASSWORD ||
      composer.GetCursor() != composer.GetLength()) {
    return nullptr;
  }
  const std::string raw = composer.GetRawString();
  const auto* entry = composer::EnglishInputExceptions::Find(
      request.config(), raw);
  if (entry == nullptr) return nullptr;
  if (entry->behavior() ==
          composer::EnglishInputExceptions::Entry::AUTO_ASCII &&
      (raw != request.key() || raw != composer.GetStringForPreedit())) {
    return nullptr;
  }
  return entry;
}

absl::string_view CanonicalEnglishFormForRequest(
    const ConversionRequest& request) {
  const auto* entry = ExceptionForRequest(request);
  return entry == nullptr ? absl::string_view() : entry->preferred();
}

int FindCandidate(const Segment& segment, absl::string_view value) {
  for (int i = 0; i < segment.candidates_size(); ++i) {
    const converter::Candidate& candidate = segment.candidate(i);
    if (candidate.value == value &&
        (candidate.attributes &
         (converter::Attribute::PARTIALLY_KEY_CONSUMED |
          converter::Attribute::TYPING_CORRECTION |
          converter::Attribute::COMMAND_CANDIDATE)) == 0) {
      return i;
    }
  }
  return -1;
}

bool PutCandidateAt(Segment* segment, absl::string_view value,
                    absl::string_view key, int target_index) {
  const int existing = FindCandidate(*segment, value);
  if (existing == target_index) return false;
  if (existing >= 0) {
    segment->move_candidate(existing, target_index);
    return true;
  }
  converter::Candidate* candidate = segment->insert_candidate(target_index);
  candidate->key = std::string(key);
  candidate->content_key = std::string(key);
  candidate->value = std::string(value);
  candidate->content_value = std::string(value);
  candidate->attributes |= (converter::Attribute::NO_VARIANTS_EXPANSION |
                            converter::Attribute::NO_EXTRA_DESCRIPTION);
  return true;
}

// A complete exact raw token may span multiple Japanese conversion segments.
// Surrounding Japanese composition ("mouGoogle"/"githubno") never qualifies.
bool PromoteCanonicalEnglishCandidate(const ConversionRequest& request,
                                      Segments* segments) {
  const auto* rule = ExceptionForRequest(request);
  if (rule == nullptr || segments->conversion_segments_size() == 0) {
    return false;
  }
  const absl::string_view canonical = rule->preferred();
  const size_t segment_count = segments->conversion_segments_size();
  if (segment_count > 1) {
    // Only a whole raw token is eligible: arbitrary Japanese sentences must
    // never be collapsed into a spelling-correction candidate.  Full-span
    // candidates are meaningful only for explicit conversion, not prediction.
    if (rule->behavior() !=
            composer::EnglishInputExceptions::Entry::CANDIDATE_ONLY ||
        request.request_type() != ConversionRequest::CONVERSION) {
      return false;
    }
    std::string joined_key;
    for (const Segment& part : segments->conversion_segments()) {
      joined_key.append(part.key());
    }
    if (joined_key != request.key()) return false;
    Segment* first = segments->mutable_conversion_segment(0);
    for (const auto& candidate : first->candidates()) {
      if (candidate->value == canonical &&
          candidate->effective_converted_segment_count() == segment_count) {
        return false;
      }
    }
    converter::Candidate* candidate = first->insert_candidate(0);
    candidate->key = std::string(request.key());
    candidate->content_key = candidate->key;
    candidate->value = std::string(canonical);
    candidate->content_value = candidate->value;
    candidate->converted_segment_count = static_cast<uint32_t>(segment_count);
    candidate->attributes |= (converter::Attribute::NO_VARIANTS_EXPANSION |
                              converter::Attribute::NO_EXTRA_DESCRIPTION);
    return true;
  }
  Segment* segment = segments->mutable_conversion_segment(0);
  if (segment->key() != request.key()) return false;
  const std::string raw = request.composer().GetRawString();
  bool modified = PutCandidateAt(segment, canonical, request.key(), 0);
  modified |= PutCandidateAt(segment, raw, request.key(), 1);

  // Offer standard English casing alternatives without changing the actual
  // keystrokes or F10 transliteration. Candidates share the exact raw reading,
  // so the existing Ctrl+Del suppression dictionary can hide each surface
  // independently (e.g. "github" -> "GitHub" without hiding "github").
  int next_index = 2;
  std::string capitalized(raw);
  Util::CapitalizeString(&capitalized);
  if (capitalized != canonical && capitalized != raw) {
    modified |= PutCandidateAt(segment, capitalized, request.key(),
                               next_index++);
  }
  std::string upper(raw);
  Util::UpperString(&upper);
  if (upper != canonical && upper != raw && upper != capitalized) {
    modified |= PutCandidateAt(segment, upper, request.key(), next_index++);
  }
  return modified;
}

bool IsAsciiIdentifierCharacter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_';
}

bool HasWordBoundaries(absl::string_view text, size_t at, size_t length) {
  return (at == 0 || !IsAsciiIdentifierCharacter(text[at - 1])) &&
         (at + length == text.size() ||
          !IsAsciiIdentifierCharacter(text[at + length]));
}

// Return the position of the sole whole-word occurrence. We can only
// rewrite a candidate when the ASCII surface is unambiguous; matching its
// first occurrence would corrupt e.g. "github ... github" candidates.
size_t FindUniqueWholeWord(absl::string_view text, absl::string_view word) {
  const size_t pos = text.find(word);
  if (pos == absl::string_view::npos ||
      !HasWordBoundaries(text, pos, word.size()) ||
      text.find(word, pos + word.size()) != absl::string_view::npos) {
    return absl::string_view::npos;
  }
  return pos;
}

// A sentence's conversion segment may cover Japanese text surrounding an
// English word. Promote a surface only when the segment's original key AND
// the composer's actual keystrokes prove the word was typed as English.
bool PromoteCanonicalEnglishInContext(const ConversionRequest& request,
                                      Segments* segments) {
  const composer::ComposerData& composer = request.composer();
  if (!request.config().enable_english_input_exceptions() ||
      request.config().preedit_method() != config::Config::ROMAN ||
      request.request().special_romanji_table() !=
          commands::Request::DEFAULT_TABLE ||
      composer.suppress_language_aware_raw_input() ||
      composer.GetInputMode() != transliteration::HIRAGANA ||
      composer.GetInputFieldType() == commands::Context::PASSWORD ||
      composer.GetCursor() != composer.GetLength() ||
      request.key() != composer.GetStringForPreedit()) {
    return false;
  }
  bool modified = false;
  const composer::EnglishInputExceptions::Index index(request.config());
  size_t segment_offset = 0;
  for (Segment& seg : segments->conversion_segments()) {
    const std::string segment_key(seg.key());
    const size_t segment_length = Util::CharsLen(segment_key);
    for (const auto& rule : request.config().english_input_exceptions()) {
      if (!rule.enabled() ||
          rule.behavior() !=
              composer::EnglishInputExceptions::Entry::AUTO_ASCII ||
          !composer::EnglishInputExceptions::IsValidEntry(rule) ||
          index.Find(rule.input()) != &rule) {
        continue;
      }
      const absl::string_view word = rule.input();
      const absl::string_view canonical = rule.preferred();
      size_t at = segment_key.find(word);
      while (at != std::string::npos) {
        const size_t character_offset =
            segment_offset + Util::CharsLen(segment_key.substr(0, at));
        if (HasWordBoundaries(segment_key, at, word.size()) &&
            composer.GetRawSubString(character_offset, word.size()) == word) {
          // Single-token segments retain all four independent surface choices.
          if (segment_key == word) {
            modified |= PutCandidateAt(&seg, canonical, word, 0);
            modified |= PutCandidateAt(&seg, word, word, 1);
          } else {
            // Only rewrite a uniquely located span. If the same bytes appear
            // anywhere else in this segment, raw-to-surface alignment is
            // ambiguous and we must not choose an occurrence by guesswork.
            if (segment_key.find(word) != at ||
                segment_key.find(word, at + word.size()) != std::string::npos) {
              break;
            }
            // The converter is free to split Japanese inflection from
            // content_value and to convert surrounding kana to kanji.  Thus
            // equality with segment_key is not a valid alignment test.  The
            // original key + raw-keystroke check above authenticates the
            // English span.  Require exactly one whole-word occurrence in
            // BOTH candidate surfaces and ensure content_value is a literal
            // substring of value: this keeps the replacement localized to
            // an existing ASCII token without guessing Japanese alignment.
            for (int candidate_index = 0;
                 candidate_index < seg.candidates_size(); ++candidate_index) {
              const converter::Candidate candidate =
                  seg.candidate(candidate_index);
              if (candidate.key != segment_key ||
                  candidate.effective_converted_segment_count() != 1 ||
                  (candidate.attributes &
                   (converter::Attribute::COMMAND_CANDIDATE |
                    converter::Attribute::TYPING_CORRECTION |
                    converter::Attribute::PARTIALLY_KEY_CONSUMED))) {
                continue;
              }
              const size_t value_pos =
                  FindUniqueWholeWord(candidate.value, word);
              const size_t content_pos =
                  FindUniqueWholeWord(candidate.content_value, word);
              if (value_pos == std::string::npos ||
                  content_pos == std::string::npos ||
                  candidate.value.find(candidate.content_value) ==
                      std::string::npos) {
                continue;
              }
              std::string canonical_value(candidate.value);
              canonical_value.replace(value_pos, word.size(), canonical);
              const int previous = FindCandidate(seg, canonical_value);
              if (previous >= 0) {
                if (previous != 0) {
                  seg.move_candidate(previous, 0);
                  modified = true;
                }
              } else {
                converter::Candidate* generated = seg.insert_candidate(0);
                *generated = candidate;
                generated->value = std::move(canonical_value);
                generated->content_value.replace(content_pos, word.size(),
                                                 canonical);
                generated->proper_noun_spans.clear();
                generated->inner_segment_boundary = {};
                generated->attributes |=
                    converter::Attribute::NO_VARIANTS_EXPANSION;
                modified = true;
              }
              break;
            }
          }
          break;
        }
        at = segment_key.find(word, at + word.size());
      }
    }
    segment_offset += segment_length;
  }
  return modified;
}

bool HasCanonicalEnglishSpan(const ConversionRequest& request) {
  if (!request.config().enable_english_input_exceptions() ||
      request.config().preedit_method() != config::Config::ROMAN ||
      request.request().special_romanji_table() !=
          commands::Request::DEFAULT_TABLE ||
      request.composer().suppress_language_aware_raw_input() ||
      request.composer().GetInputMode() != transliteration::HIRAGANA ||
      request.composer().GetInputFieldType() == commands::Context::PASSWORD ||
      request.composer().GetCursor() != request.composer().GetLength()) {
    return false;
  }

  // Candidate-only entries intentionally need no English preedit. However,
  // suggestions still require an *exact* raw-token match (not a substring).
  if (ExceptionForRequest(request) != nullptr) return true;

  const composer::ComposerData& composer = request.composer();
  const std::string preedit = composer.GetStringForPreedit();
  if (request.key() != preedit) return false;
  const composer::EnglishInputExceptions::Index index(request.config());
  for (const auto& rule : request.config().english_input_exceptions()) {
    if (!rule.enabled() ||
        rule.behavior() !=
            composer::EnglishInputExceptions::Entry::AUTO_ASCII ||
        !composer::EnglishInputExceptions::IsValidEntry(rule) ||
        index.Find(rule.input()) != &rule) {
      continue;
    }
    const absl::string_view word = rule.input();
    size_t at = preedit.find(word);
    while (at != std::string::npos) {
      if (HasWordBoundaries(preedit, at, word.size())) {
        const size_t char_start =
            Util::CharsLen(absl::string_view(preedit).substr(0, at));
        if (composer.GetRawSubString(char_start, word.size()) == word) {
          return true;
        }
      }
      at = preedit.find(word, at + word.size());
    }
  }
  return false;
}

// A trailing unresolved consonant changes the prediction key: Mozc may
// trim the suffix for suggestion while the visible preedit still includes it.
// In that state the registered-word check can fail, allowing a whole-input
// raw-ASCII rescue to appear among otherwise Japanese suggestions.
// Match the actual pending Roman state, not just an alphabetic last character
// (which could be an intentional English token or a custom Roman-table rule).
bool HasMixedJapanesePendingRoman(const ConversionRequest& request) {
  if (request.request_type() != ConversionRequest::SUGGESTION ||
      !request.config().enable_english_input_exceptions() ||
      request.config().preedit_method() != config::Config::ROMAN ||
      request.request().special_romanji_table() !=
          commands::Request::DEFAULT_TABLE) {
    return false;
  }
  const composer::ComposerData& composer = request.composer();
  if (composer.suppress_language_aware_raw_input() ||
      composer.GetInputMode() != transliteration::HIRAGANA ||
      composer.GetInputFieldType() == commands::Context::PASSWORD ||
      composer.GetCursor() != composer.GetLength() ||
      composer.GetPendingRomanInputLength() == 0) {
    return false;
  }
  const std::string raw = composer.GetRawString();
  const std::string preedit = composer.GetStringForPreedit();
  // This fallback is intended for an ongoing Japanese sentence.  A raw
  // Roman-leading fragment (e.g. "xgithuby") may have a pending suffix and
  // non-ASCII characters in the middle, but must not enable suggestions when
  // no canonical English token satisfies the visible word-boundary rules.
  // Check a Unicode character, not one UTF-8 byte.  A leading full-width
  // Latin letter is non-ASCII but is not Japanese text; accepting it would
  // enable suggestions for a Roman fragment such as "xgithuby".
  if (preedit.empty()) return false;
  const Util::ScriptType initial_script = Util::GetFirstScriptType(preedit);
  if (initial_script != Util::HIRAGANA &&
      initial_script != Util::KATAKANA &&
      initial_script != Util::KANJI) {
    return false;
  }
  return !raw.empty() && raw != preedit && Util::IsAscii(raw) &&
         !Util::IsAscii(preedit);
}

// Suppress only the exact full-input raw keystroke candidate in passive
// suggestion. Never change explicit conversion/F10 or ASCII-only suggestions.
// An unresolved suffix may make request.key() shorter than the preedit.
bool SuppressWholeRawRomajiSuggestion(const ConversionRequest& request,
                                     Segments* segments) {
  if (request.request_type() != ConversionRequest::SUGGESTION ||
      segments->conversion_segments_size() != 1) {
    return false;
  }
  const bool pending_roman = HasMixedJapanesePendingRoman(request);
  if (!pending_roman && !HasCanonicalEnglishSpan(request)) {
    return false;
  }
  const composer::ComposerData& composer = request.composer();
  const std::string raw = composer.GetRawString();
  const std::string preedit = composer.GetStringForPreedit();
  if (raw.empty() || raw == preedit || !Util::IsAscii(raw) ||
      Util::IsAscii(preedit) ||
      (!pending_roman && request.key() != preedit)) {
    return false;
  }
  Segment* segment = segments->mutable_conversion_segment(0);
  if (segment->key() != request.key()) return false;
  bool modified = false;
  for (int i = static_cast<int>(segment->candidates_size()) - 1; i >= 0; --i) {
    const converter::Candidate& candidate = segment->candidate(i);
    if (candidate.value == raw && candidate.content_value == raw &&
        !(candidate.attributes & converter::Attribute::COMMAND_CANDIDATE)) {
      segment->erase_candidate(i);
      modified = true;
    }
  }
  return modified;
}

}  // namespace

// Add space-prefixed variants to enable Space-joined English words conversion.
// 'Google Japan' consists of 'ぐーぐる': 'Google', and 'じゃぱん': 'Japan'
// Therefore, when a user types 'ぐーぐるじゃぱん', it will be 'GoogleJapan'
// instead. To avoid this, in non-first segments which follow english word
// segments, add these space-prefixed candidates.
bool EnglishVariantsRewriter::ExpandSpacePrefixedVariants(
    const absl::string_view input, std::vector<std::string>* variants) const {
  DCHECK(variants);

  if (input.empty()) {
    return false;
  }
  if (input.starts_with(' ')) {
    return false;
  }
  std::vector<std::string> space_prefixed_variants;
  space_prefixed_variants.push_back(absl::StrCat(" ", input));
  for (const std::string& word : *variants) {
    if (word.empty()) {
      continue;
    }
    if (!word.starts_with(' ')) {
      space_prefixed_variants.push_back(word);
      space_prefixed_variants.push_back(absl::StrCat(" ", word));
    } else {
      space_prefixed_variants.push_back(word);
    }
  }
  // replace the content of variants
  *variants = std::move(space_prefixed_variants);
  return true;
}

bool EnglishVariantsRewriter::ExpandEnglishVariants(
    const absl::string_view input, std::vector<std::string>* variants) const {
  DCHECK(variants);

  if (input.empty()) {
    return false;
  }

  // multi-word
  if (absl::StrContains(input, " ")) {
    return false;
  }

  std::string lower(input);
  std::string upper(input);
  std::string capitalized(input);
  Util::LowerString(&lower);
  Util::UpperString(&upper);
  Util::CapitalizeString(&capitalized);

  if (lower == upper) {
    // given word is non-ascii.
    return false;
  }

  variants->clear();
  // If |input| is non-standard expression, like "iMac", only
  // expand lowercase.
  if (input != lower && input != upper && input != capitalized) {
    variants->push_back(lower);
    return true;
  }

  if (input != lower) {
    variants->push_back(lower);
  }
  if (input != capitalized) {
    variants->push_back(capitalized);
  }
  if (input != upper) {
    variants->push_back(upper);
  }

  return true;
}

bool EnglishVariantsRewriter::IsT13NCandidate(
    converter::Candidate* candidate) const {
  return (Util::IsEnglishTransliteration(candidate->content_value) &&
          Util::GetScriptType(candidate->content_key) == Util::HIRAGANA);
}

bool EnglishVariantsRewriter::IsEnglishCandidate(
    converter::Candidate* candidate) const {
  return (Util::IsEnglishTransliteration(candidate->content_value) &&
          Util::GetScriptType(candidate->content_key) == Util::ALPHABET);
}

bool EnglishVariantsRewriter::ExpandEnglishVariantsWithSegment(
    bool need_space_prefix, Segment* seg) const {
  CHECK(seg);

  bool modified = false;
  absl::flat_hash_set<std::string> expanded_t13n_candidates;
  absl::flat_hash_set<std::string> original_candidates;

  for (size_t i = 0; i < seg->candidates_size(); ++i) {
    original_candidates.insert(seg->candidate(i).value);
  }

  for (int i = seg->candidates_size() - 1; i >= 0; --i) {
    converter::Candidate* original_candidate = seg->mutable_candidate(i);
    DCHECK(original_candidate);

    // http://b/issue?id=5137299
    // If the entry is coming from user dictionary,
    // expand English variants.
    if (original_candidate->attributes &
            converter::Attribute::NO_VARIANTS_EXPANSION &&
        !(original_candidate->attributes &
          converter::Attribute::USER_DICTIONARY)) {
      continue;
    }

    if (IsT13NCandidate(original_candidate)) {
      if (!(original_candidate->attributes &
            converter::Attribute::NO_VARIANTS_EXPANSION)) {
        modified = true;
        original_candidate->attributes |=
            converter::Attribute::NO_VARIANTS_EXPANSION;
      }

      if (expanded_t13n_candidates.find(original_candidate->value) !=
          expanded_t13n_candidates.end()) {
        continue;
      }

      const bool is_proper_noun =
          (original_candidate->lid == original_candidate->rid &&
           pos_matcher_.IsUniqueNoun(original_candidate->lid));
      if (is_proper_noun && Util::IsUpperAscii(original_candidate->value)) {
        // We do not have to expand upper case proper nouns (ex. NASA).
        // Note:
        // It is very popular that some company or service name is written in
        // lower case, but their formal form is capital case (ex. google)
        // so we suppress expansion only for capital case here.
        continue;
      }

      // Expand T13N candidate variants
      std::vector<std::string> variants;
      bool expanded =
          ExpandEnglishVariants(original_candidate->content_value, &variants);
      if (need_space_prefix) {
        expanded |= ExpandSpacePrefixedVariants(
            original_candidate->content_value, &variants);
      }
      if (expanded) {
        CHECK(!variants.empty());
        for (auto it = variants.rbegin(); it != variants.rend(); ++it) {
          const std::string new_value =
              absl::StrCat(*it, original_candidate->functional_value());
          expanded_t13n_candidates.insert(new_value);
          if (original_candidates.find(new_value) !=
              original_candidates.end()) {
            continue;
          }
          modified = true;

          converter::Candidate* new_candidate = seg->insert_candidate(i + 1);
          DCHECK(new_candidate);
          new_candidate->value = std::move(new_value);
          new_candidate->key = original_candidate->key;
          new_candidate->content_value = std::move(*it);
          new_candidate->content_key = original_candidate->content_key;
          new_candidate->cost = original_candidate->cost;
          new_candidate->wcost = original_candidate->wcost;
          new_candidate->structure_cost = original_candidate->structure_cost;
          new_candidate->lid = original_candidate->lid;
          new_candidate->rid = original_candidate->rid;
          new_candidate->attributes |=
              converter::Attribute::NO_VARIANTS_EXPANSION;
          if (original_candidate->attributes &
              converter::Attribute::PARTIALLY_KEY_CONSUMED) {
            new_candidate->attributes |=
                converter::Attribute::PARTIALLY_KEY_CONSUMED;
            new_candidate->consumed_key_size =
                original_candidate->consumed_key_size;
          }
        }
      }
    } else if (IsEnglishCandidate(original_candidate)) {
      // Fix variants for English candidate
      modified = true;
      original_candidate->attributes |=
          converter::Attribute::NO_VARIANTS_EXPANSION;
    }
  }

  return modified;
}

int EnglishVariantsRewriter::capability(
    const ConversionRequest& request) const {
  if (request.request().mixed_conversion() ||
      HasCanonicalEnglishSpan(request) ||
      HasMixedJapanesePendingRoman(request)) {
    return RewriterInterface::ALL;
  }
  return RewriterInterface::CONVERSION;
}

bool EnglishVariantsRewriter::Rewrite(const ConversionRequest& request,
                                      Segments* segments) const {
  // Pending-Roman rescue suppression is the only new suggestion behavior.
  // Do not expand variants for ordinary Japanese predictions in this state.
  if (HasMixedJapanesePendingRoman(request) &&
      !HasCanonicalEnglishSpan(request)) {
    return SuppressWholeRawRomajiSuggestion(request, segments);
  }
  const commands::DecoderExperimentParams& params =
      request.request().decoder_experiment_params();
  // 1: enable space insertion
  const bool enable_space_insersion =
      params.english_variation_space_insertion_mode() == 1;
  bool modified = false;
  bool is_previous_candidate_english = false;
  for (Segment& segment : segments->conversion_segments()) {
    // if the top candidate of previous segment is an english word,
    // need_space_prefix = true
    const bool need_space_prefix =
        enable_space_insersion && is_previous_candidate_english;
    modified |= ExpandEnglishVariantsWithSegment(need_space_prefix, &segment);
    is_previous_candidate_english =
        segment.candidates_size() > 0 &&
        Util::IsScriptType(segment.candidate(0).value, Util::ALPHABET);
  }

  modified |= PromoteCanonicalEnglishCandidate(request, segments);
  modified |= PromoteCanonicalEnglishInContext(request, segments);
  modified |= SuppressWholeRawRomajiSuggestion(request, segments);
  return modified;
}
}  // namespace mozc
