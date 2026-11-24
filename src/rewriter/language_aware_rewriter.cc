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

#include "rewriter/language_aware_rewriter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "base/japanese_util.h"
#include "base/util.h"
#include "composer/composer.h"
#include "composer/english_input_exceptions.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "dictionary/dictionary_interface.h"
#include "dictionary/pos_matcher.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"
#include "transliteration/transliteration.h"

namespace mozc {

using dictionary::DictionaryInterface;
using dictionary::PosMatcher;

LanguageAwareRewriter::LanguageAwareRewriter(
    const PosMatcher& pos_matcher, const DictionaryInterface& dictionary)
    : unknown_id_(pos_matcher.GetUnknownId()), dictionary_(&dictionary) {}

LanguageAwareRewriter::~LanguageAwareRewriter() = default;

namespace {

bool IsMobileRequest(const ConversionRequest& request) {
  return request.request().zero_query_suggestion() &&
         request.request().mixed_conversion();
}

bool IsRomanHiraganaInput(const ConversionRequest& request) {
  const auto table = request.request().special_romanji_table();
  switch (table) {
    case commands::Request::DEFAULT_TABLE:
      return (request.config().preedit_method() == config::Config::ROMAN);
    case commands::Request::QWERTY_MOBILE_TO_HIRAGANA:
      return true;
    default:
      return false;
  }
}

bool IsEnabled(const ConversionRequest& request) {
  // Explicit Japanese-mode selection within a composition takes precedence
  // over the raw-ASCII rescue heuristic. The raw text itself remains intact
  // for explicit ASCII transliteration (e.g. F10).
  if (request.composer().suppress_language_aware_raw_input()) {
    return false;
  }
  const auto mode = request.request().language_aware_input();
  if (mode == commands::Request::NO_LANGUAGE_AWARE_INPUT) {
    return false;
  }
  if (mode == commands::Request::LANGUAGE_AWARE_SUGGESTION) {
    return IsRomanHiraganaInput(request);
  }
  DCHECK_EQ(commands::Request::DEFAULT_LANGUAGE_AWARE_BEHAVIOR, mode);
  return request.config().use_spelling_correction();
}

}  // namespace

int LanguageAwareRewriter::capability(const ConversionRequest& request) const {
  // Language aware input is performed only on suggestion or prediction.
  if (!IsEnabled(request)) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  return (RewriterInterface::SUGGESTION | RewriterInterface::PREDICTION);
}

namespace {
// Only an explicitly registered AUTO_ASCII token followed by Japanese input
// identifies a mixed-script composition whose full raw keystrokes should not
// appear in passive suggestions (e.g. "githubwot" -> "githubをt").
// A shifted ASCII prefix is not sufficient evidence: "AIdev" -> "AIでv"
// must retain its original raw-ASCII rescue candidate.
bool HasRegisteredEnglishWordFollowedByJapanese(
    const composer::ComposerData& composer, const config::Config& config) {
  const std::string preedit = composer.GetStringForPreedit();
  size_t latin_start = 0;
  size_t latin_letters = 0;
  size_t char_offset = 0;
  for (size_t i = 0; i < preedit.size();) {
    const unsigned char byte = static_cast<unsigned char>(preedit[i]);
    if ((byte >= 'a' && byte <= 'z') ||
        (byte >= 'A' && byte <= 'Z')) {
      if (latin_letters == 0) latin_start = i;
      ++latin_letters;
      ++char_offset;
      ++i;
      continue;
    }
    size_t char_bytes = 0;
    const Util::ScriptType script =
        Util::GetFirstScriptType(absl::string_view(preedit).substr(i),
                                 &char_bytes);
    if (latin_letters >= 2 &&
        (script == Util::HIRAGANA || script == Util::KATAKANA ||
         script == Util::KANJI)) {
      const std::string word = preedit.substr(latin_start, latin_letters);
      const auto* rule =
          composer::EnglishInputExceptions::Find(config, word);
      if (rule != nullptr &&
          rule->behavior() ==
              config::Config::EnglishInputException::AUTO_ASCII &&
          composer.GetRawSubString(char_offset - latin_letters,
                                   latin_letters) == word) {
        return true;
      }
    }
    latin_letters = 0;
    ++char_offset;
    i += char_bytes == 0 ? 1 : char_bytes;
  }
  return false;
}

bool IsRawQuery(const composer::ComposerData& composer,
                const DictionaryInterface* dictionary, int* rank) {
  const std::string raw_text = composer.GetRawString();

  // Check if the length of text is less than or equal to three.
  // For example, "cat" is not treated as a raw query so far to avoid
  // false negative cases.
  if (raw_text.size() <= 3) {
    return false;
  }

  // If the composition string is same with the raw_text, there is no
  // need to add the candidate to suggestions.
  const std::string composition = composer.GetStringForPreedit();
  if (composition == raw_text) {
    return false;
  }

  // If the composition string is the full width form of the raw_text,
  // there is no need to add the candidate to suggestions.
  std::string composition_in_half_width_ascii =
      japanese_util::FullWidthAsciiToHalfWidthAscii(composition);
  if (composition_in_half_width_ascii == raw_text) {
    return false;
  }

  // If alphabet characters are in the middle of the composition, it is
  // probably a raw query.  For example, "えぁｍｐぇ" (example) contains
  // "m" and "p" in the middle.  So it is treated as a raw query.  On the
  // other hand, "くえｒｙ" (query) contains alphabet characters, but they
  // are at the end of the string, so it cannot be determined here.
  //
  // Note, GetQueryForPrediction omits the trailing alphabet characters of
  // the composition string and returns it.
  const std::string key = composer.GetQueryForPrediction();
  if (Util::ContainsScriptType(key, Util::ALPHABET)) {
    // A known Japanese reading followed by literal English ("もうGoogle")
    // should outrank its raw keystrokes ("mouGoogle"). An unknown Japanese
    // prefix ("あぞおKey" from "azooKey") still needs the raw rescue at top.
    // Test the preedit, not the final dictionary key: the latter may combine
    // Japanese and Latin runs that do not form a dictionary key as a whole.
    const size_t latin_pos = composition_in_half_width_ascii.find_first_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    // A dictionary key is not proof of an independently intended Japanese
    // word: single-kana readings such as "い" or "あ" also have entries.
    // Treat short Roman prefixes conservatively, so iPhone/iPad/aGoogle
    // keep the original ASCII rescue at rank 0.  The longer "mou" -> "もう"
    // prefix can still favor the mixed-script preedit "もうGoogle".
    // Check the literal suffix against the raw input instead of inferring
    // a Roman boundary from UTF-8 byte offsets in the Japanese preedit.
    bool prefer_japanese_preedit = false;
    if (latin_pos != std::string::npos && latin_pos != 0) {
      const std::string japanese_prefix =
          composition_in_half_width_ascii.substr(0, latin_pos);
      const std::string latin_suffix =
          composition_in_half_width_ascii.substr(latin_pos);
      constexpr size_t kMinJapanesePrefixChars = 2;
      constexpr size_t kMinRomanPrefixBytes = 3;
      if (Util::CharsLen(japanese_prefix) >= kMinJapanesePrefixChars &&
          raw_text.size() >= latin_suffix.size() + kMinRomanPrefixBytes &&
          raw_text.compare(raw_text.size() - latin_suffix.size(),
                           latin_suffix.size(), latin_suffix) == 0 &&
          dictionary->HasKey(japanese_prefix)) {
        prefer_japanese_preedit = true;
      }
    }
    *rank = prefer_japanese_preedit ? 1 : 0;
    return true;
  }

  // If the composition is storead as a key in the dictionary like
  // "はな" (hana), "たけ" (take), the query is not handled as a raw query.
  // It is a little conservative, but a safer way.
  if (dictionary->HasKey(key)) {
    return false;
  }

  // If the input text is stored in the dictionary, it is perhaps a raw query.
  // For example, the input characters of "れもヴぇ" (remove) is in the
  // dictionary, so it is treated as a raw text.
  if (dictionary->HasValue(raw_text)) {
    *rank = 2;
    return true;
  }

  return false;
}

// Get T13n candidate ids from existing candidates.
void GetAlphabetIds(const Segment& segment, uint16_t* lid, uint16_t* rid) {
  DCHECK(lid);
  DCHECK(rid);

  for (int i = 0; i < segment.candidates_size(); ++i) {
    const converter::Candidate& candidate = segment.candidate(i);
    const Util::ScriptType type = Util::GetScriptType(candidate.value);
    if (type == Util::ALPHABET) {
      *lid = candidate.lid;
      *rid = candidate.rid;
      return;
    }
  }
}
}  // namespace

// Note: This function seemed slow, but the benchmark tests
// resulted that it was only less than 0.1% point penalty.
// = session_handler_benchmark_test
// BM_PerformanceForRandomKeyEvents: 891944807 -> 892740748 (1.00089)
// = converter_benchmark_test
// BM_DesktopAnthyCorpusConversion 25062440090 -> 25101542382 (1.002)
// BM_DesktopStationPredictionCorpusPrediction 8695341697 -> 8672187681 (0.997)
// BM_DesktopStationPredictionCorpusSuggestion 6149502840 -> 6152393270 (1.000)
bool LanguageAwareRewriter::FillRawText(const ConversionRequest& request,
                                        Segments* segments) const {
  if (segments->conversion_segments_size() != 1) {
    return false;
  }

  // Do not insert the entire raw keystroke sequence (e.g. "githubwot")
  // while composing intentional mixed Japanese + literal English text
  // ("githubをｔ"). The existing language-aware rescue is still useful for
  // truly mis-typed English ("python", "iPhone") and is untouched there.
  // This applies only to passive suggestions with an unresolved Roman tail.
  if (request.request_type() == ConversionRequest::SUGGESTION &&
      request.config().enable_english_input_exceptions() &&
      request.config().preedit_method() == config::Config::ROMAN &&
      request.request().special_romanji_table() ==
          commands::Request::DEFAULT_TABLE) {
    const composer::ComposerData& composer = request.composer();
    if (!composer.suppress_language_aware_raw_input() &&
        composer.GetInputMode() == transliteration::HIRAGANA &&
        composer.GetInputFieldType() != commands::Context::PASSWORD &&
        composer.GetCursor() == composer.GetLength() &&
        composer.GetPendingRomanInputLength() != 0 &&
        HasRegisteredEnglishWordFollowedByJapanese(
            composer, request.config())) {
      return false;
    }
  }

  int rank = 0;
  if (!IsRawQuery(request.composer(), dictionary_, &rank)) {
    return false;
  }

  Segment* segment = segments->mutable_conversion_segment(0);

  // Language aware candidates are useful on desktop as users may forget
  // switching the IME. However, on mobile software keyboard, such mistakes do
  // rarely occur, so we can fix the position for the sake of consistency.
  if (IsMobileRequest(request)) {
    rank = 2;
    // Do no insert the new candidate over the typing corrections.
    while (rank < segment->candidates_size()) {
      if (!(segment->candidate(rank).attributes &
            converter::Attribute::TYPING_CORRECTION)) {
        break;
      }
      ++rank;
    }
  }

  const std::string raw_string = request.composer().GetRawString();

  uint16_t lid = unknown_id_;
  uint16_t rid = unknown_id_;
  GetAlphabetIds(*segment, &lid, &rid);

  // On desktop, rank 1 signals a known Japanese reading before a literal
  // alphabet run, such as "もうGoogle" from "mouGoogle". Ranking raw input
  // behind the first candidate is insufficient when the predictor returned
  // no candidate for this mixed-script composition. Ensure the user's exact
  // preedit is selectable even in that case; preserve native candidates
  // when they already supply the same complete value.
  if (rank == 1 && !IsMobileRequest(request)) {
    const std::string preedit = request.composer().GetStringForPreedit();
    int existing_index = -1;
    for (int i = 0; i < segment->candidates_size(); ++i) {
      const converter::Candidate& existing = segment->candidate(i);
      if (existing.value == preedit &&
          (existing.attributes &
           (converter::Attribute::PARTIALLY_KEY_CONSUMED |
            converter::Attribute::TYPING_CORRECTION |
            converter::Attribute::COMMAND_CANDIDATE)) == 0) {
        existing_index = i;
        break;
      }
    }
    if (existing_index >= 0) {
      if (existing_index != 0) {
        segment->move_candidate(existing_index, 0);
      }
    } else {
      converter::Candidate* preedit_candidate = segment->insert_candidate(0);
      preedit_candidate->key = request.key();
      preedit_candidate->content_key = request.key();
      preedit_candidate->value = preedit;
      preedit_candidate->content_value = preedit;
      preedit_candidate->lid = unknown_id_;
      preedit_candidate->rid = unknown_id_;
      preedit_candidate->attributes |=
          converter::Attribute::NO_VARIANTS_EXPANSION |
          converter::Attribute::NO_EXTRA_DESCRIPTION;
    }
  }

  // Create a candidate.
  if (rank > segment->candidates_size()) {
    rank = segment->candidates_size();
  }
  converter::Candidate* candidate = segment->insert_candidate(rank);
  candidate->value = raw_string;
  candidate->key = raw_string;
  candidate->content_value = raw_string;
  // raw_string is no longer used, so move it.
  candidate->content_key = std::move(raw_string);
  candidate->lid = lid;
  candidate->rid = rid;

  candidate->attributes |= (converter::Attribute::NO_VARIANTS_EXPANSION |
                            converter::Attribute::NO_EXTRA_DESCRIPTION);

  return true;
}

bool LanguageAwareRewriter::Rewrite(const ConversionRequest& request,
                                    Segments* segments) const {
  if (!IsEnabled(request)) {
    return false;
  }
  return FillRawText(request, segments);
}

}  // namespace mozc
