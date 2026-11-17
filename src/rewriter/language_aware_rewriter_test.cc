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
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "composer/composer.h"
#include "composer/table.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "converter/segments_matchers.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/dictionary_mock.h"
#include "dictionary/pos_matcher.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

using ::mozc::dictionary::MockDictionary;
using ::mozc::dictionary::PosMatcher;
using ::testing::AllOf;
using ::testing::Field;
using ::testing::Matcher;
using ::testing::Mock;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::StrEq;

void InsertASCIISequence(const absl::string_view text,
                         composer::Composer* composer) {
  for (size_t i = 0; i < text.size(); ++i) {
    commands::KeyEvent key;
    key.set_key_code(text[i]);
    composer->InsertCharacterKeyEvent(key);
  }
}

class LanguageAwareRewriterTest : public testing::TestWithTempUserProfile {
 protected:
  bool RewriteWithLanguageAwareInput(const LanguageAwareRewriter* rewriter,
                                     const absl::string_view key,
                                     bool is_mobile, std::string* composition,
                                     Segments* segments) {
    commands::Request client_request;
    client_request.set_language_aware_input(
        commands::Request::LANGUAGE_AWARE_SUGGESTION);

    if (is_mobile) {
      client_request.set_zero_query_suggestion(true);
      client_request.set_mixed_conversion(true);
    }

    auto table = std::make_shared<composer::Table>();
    config::Config default_config;
    table->InitializeWithRequestAndConfig(client_request, default_config);

    composer::Composer composer(table, client_request, default_config);
    InsertASCIISequence(key, &composer);
    *composition = composer.GetStringForPreedit();

    // Perform the rewrite command.
    if (segments->conversion_segments_size() == 0) {
      segments->add_segment();
    }
    Segment* segment = segments->mutable_conversion_segment(0);
    segment->set_key(*composition);
    const ConversionRequest request =
        ConversionRequestBuilder()
            .SetComposer(composer)
            .SetRequest(client_request)
            .SetRequestType(ConversionRequest::SUGGESTION)
            .Build();

    return rewriter->Rewrite(request, segments);
  }

  const testing::MockDataManager data_manager_;
};

void PushFrontCandidate(const absl::string_view data, Segment* segment) {
  converter::Candidate* candidate = segment->push_front_candidate();
  candidate->value = std::string(data);
  candidate->key = std::string(data);
  candidate->content_value = std::string(data);
  candidate->content_key = std::string(data);
}

void PushFrontCandidate(const absl::string_view data, int attributes,
                        Segment* segment) {
  PushFrontCandidate(data, segment);
  if (attributes) {
    segment->mutable_candidate(0)->attributes |= attributes;
  }
}

// A matcher for converter::Candidate to test if a candidate has the given
// value.
constexpr auto ValueIs =
    [](const auto& matcher) -> Matcher<const converter::Candidate*> {
  return Field(&converter::Candidate::value, matcher);
};

// A matcher for converter::Candidate to test if a candidate has the given value
// with "did you mean" annotation.
constexpr auto IsLangAwareCandidate =
    [](absl::string_view value) -> Matcher<const converter::Candidate*> {
  return Pointee(AllOf(Field(&converter::Candidate::key, value),
                       Field(&converter::Candidate::value, value)));
};

TEST_F(LanguageAwareRewriterTest, LanguageAwareInput) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);
  {
    // "python" is composed to "ｐｙてょｎ", but "python" should be suggested,
    // because alphabet characters are in the middle of the word.
    std::string composition;
    Segments segments;
    EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "python", false,
                                              &composition, &segments));

    EXPECT_EQ(composition, "ｐｙてょｎ");
    ASSERT_EQ(segments.conversion_segments_size(), 1);
    EXPECT_THAT(segments.conversion_segment(0),
                HasSingleCandidate(IsLangAwareCandidate("python")));
    Mock::VerifyAndClearExpectations(&dictionary);
  }
  {
    // On mobile, we insert the new candidate at the third position.
    std::string composition;
    Segments segments;
    Segment* segment = segments.push_back_segment();
    PushFrontCandidate("cand3", segment);
    PushFrontCandidate("cand2", segment);
    PushFrontCandidate("cand1", segment);
    PushFrontCandidate("cand0", segment);
    ASSERT_EQ(segment->candidates_size(), 4);
    EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "python", true,
                                              &composition, &segments));
    EXPECT_EQ(composition, "ｐｙてょｎ");
    ASSERT_EQ(segments.conversion_segments_size(), 1);
    EXPECT_EQ(segments.conversion_segment(0).candidate(2).value, "python");
    EXPECT_TRUE(segments.conversion_segment(0).candidate(2).prefix.empty());
    Mock::VerifyAndClearExpectations(&dictionary);
  }
  {
    // On mobile, the candidate is inserted after the typing correction.
    std::string composition;
    Segments segments;
    Segment* segment = segments.push_back_segment();
    PushFrontCandidate("cand4", segment);
    PushFrontCandidate("cand3", converter::Attribute::TYPING_CORRECTION,
                       segment);
    PushFrontCandidate("cand2", converter::Attribute::TYPING_CORRECTION,
                       segment);
    PushFrontCandidate("cand1", segment);
    PushFrontCandidate("cand0", segment);
    ASSERT_EQ(segment->candidates_size(), 5);
    EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "python",
                                              true, /* is_mobile */
                                              &composition, &segments));
    EXPECT_EQ(composition, "ｐｙてょｎ");
    ASSERT_EQ(segments.conversion_segments_size(), 1);
    EXPECT_EQ(segments.conversion_segment(0).candidate(4).value, "python");
    EXPECT_TRUE(segments.conversion_segment(0).candidate(4).prefix.empty());
    Mock::VerifyAndClearExpectations(&dictionary);
  }
  {
    // "mozuk" is composed to "もずｋ", then "mozuk" is not suggested.
    // The trailing alphabet characters are not counted.
    std::string composition;
    Segments segments;
    EXPECT_FALSE(RewriteWithLanguageAwareInput(
        &rewriter, "mozuk", false, /* is_mobile */ &composition, &segments));

    EXPECT_EQ(composition, "もずｋ");
    ASSERT_EQ(segments.conversion_segments_size(), 1);
    EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 0);
    Mock::VerifyAndClearExpectations(&dictionary);
  }
  {
    // "house" is composed to "ほうせ".  Since "house" is in the dictionary
    // unlike the above "mozuk" case, "house" should be suggested.
    std::string composition;
    Segments segments;
    Segment* segment = segments.push_back_segment();
    // Add three candidates: ["cand0", "cand1", "cand2"].
    PushFrontCandidate("cand2", segment);
    PushFrontCandidate("cand1", segment);
    PushFrontCandidate("cand0", segment);
    ASSERT_EQ(segment->candidates_size(), 3);

    // Set up the mock dictionary: "ほうせ" -> "house" doesn't exist but there's
    // an entry whose value is "house".
    EXPECT_CALL(dictionary, HasKey(StrEq("ほうせ"))).WillOnce(Return(false));
    EXPECT_CALL(dictionary, HasValue(StrEq("house"))).WillOnce(Return(true));

    // "house" should be inserted as the 3rd candidate (b/w cand1 and cand2).
    // => ["cand0", "cand1", "house", "cand2"]
    EXPECT_TRUE(RewriteWithLanguageAwareInput(
        &rewriter, "house", false, /* is_mobile */ &composition, &segments));
    EXPECT_EQ(composition, "ほうせ");
    EXPECT_THAT(*segment, CandidatesAreArray({
                              ValueIs("cand0"),
                              ValueIs("cand1"),
                              IsLangAwareCandidate("house"),
                              ValueIs("cand2"),
                          }));
    Mock::VerifyAndClearExpectations(&dictionary);
  }
  {
    // Set up the mock dictionary: there's an entry whose value is "house".
    EXPECT_CALL(dictionary, HasValue(StrEq("query"))).WillOnce(Return(true));

    // "query" is composed to "くえｒｙ".  Since "query" is in the dictionary
    // unlike the above "mozuk" case, "query" should be suggested.
    std::string composition;
    Segments segments;
    EXPECT_TRUE(RewriteWithLanguageAwareInput(
        &rewriter, "query", false, /* is_mobile */ &composition, &segments));

    EXPECT_EQ(composition, "くえｒｙ");
    ASSERT_EQ(segments.conversion_segments_size(), 1);
    EXPECT_THAT(segments.conversion_segment(0),
                HasSingleCandidate(IsLangAwareCandidate("query")));
    Mock::VerifyAndClearExpectations(&dictionary);
  }
  {
    // "google" is composed to "google" by mode_switching_handler.
    // If the suggestion is equal to the composition, that suggestion
    // is not added.
    std::string composition;
    Segments segments;
    EXPECT_FALSE(RewriteWithLanguageAwareInput(&rewriter, "google",
                                               false, /* is_mobile */
                                               &composition, &segments));
    EXPECT_EQ(composition, "google");
    Mock::VerifyAndClearExpectations(&dictionary);
  }
  {
    // Set up the mock dictionary.
    EXPECT_CALL(dictionary, HasKey(StrEq("なる"))).WillRepeatedly(Return(true));
    EXPECT_CALL(dictionary, HasValue(StrEq("なる")))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(dictionary, HasValue(StrEq("naru")))
        .WillRepeatedly(Return(true));

    // The key "なる" has two value "naru" and "なる".
    // In this case, language aware rewriter should not be triggered.
    std::string composition;
    Segments segments;
    EXPECT_FALSE(RewriteWithLanguageAwareInput(
        &rewriter, "naru", false, /* is_mobile */ &composition, &segments));

    EXPECT_EQ(composition, "なる");
    ASSERT_EQ(segments.conversion_segments_size(), 1);
    EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 0);
    Mock::VerifyAndClearExpectations(&dictionary);
  }
}

TEST_F(LanguageAwareRewriterTest, KnownJapanesePrefixOutranksRawLatin) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);

  // The standard candidate is more useful than raw "mouGoogle".
  Segments japanese_segments;
  PushFrontCandidate("もうGoogle", japanese_segments.add_segment());
  std::string composition;
  EXPECT_CALL(dictionary, HasKey(StrEq("もう"))).WillOnce(Return(true));
  EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "mouGoogle", false,
                                            &composition, &japanese_segments));
  EXPECT_EQ(composition, "もうGoogle");
  ASSERT_EQ(japanese_segments.conversion_segment(0).candidates_size(), 2);
  EXPECT_EQ(japanese_segments.conversion_segment(0).candidate(0).value,
            "もうGoogle");
  EXPECT_EQ(japanese_segments.conversion_segment(0).candidate(1).value,
            "mouGoogle");
  Mock::VerifyAndClearExpectations(&dictionary);

  // An unknown-name prefix must not demote raw "azooKey".
  Segments name_segments;
  PushFrontCandidate("あぞおKey", name_segments.add_segment());
  EXPECT_CALL(dictionary, HasKey(StrEq("あぞお"))).WillOnce(Return(false));
  EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "azooKey", false,
                                            &composition, &name_segments));
  EXPECT_EQ(composition, "あぞおKey");
  EXPECT_EQ(name_segments.conversion_segment(0).candidate(0).value,
            "azooKey");
  Mock::VerifyAndClearExpectations(&dictionary);
}

// Short Roman prefixes are ambiguous even if their kana readings appear in
// the dictionary.  In mixed-case product names, raw ASCII must remain first.
TEST_F(LanguageAwareRewriterTest, ShortRomanPrefixKeepsAsciiRescueFirst) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);

  struct TestCase {
    const char* raw;
    const char* preedit;
  };
  constexpr TestCase kCases[] = {
      {"iPhone", "いPhone"},
      {"iPad", "いPad"},
      {"aGoogle", "あGoogle"},
  };

  // A one-character Japanese prefix cannot prove Japanese-word intent;
  // do not even ask the dictionary to promote these preedits.
  EXPECT_CALL(dictionary, HasKey(::testing::_)).Times(0);
  for (const TestCase& test : kCases) {
    Segments segments;
    std::string composition;
    EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, test.raw, false,
                                              &composition, &segments))
        << test.raw;
    EXPECT_EQ(composition, test.preedit) << test.raw;
    ASSERT_EQ(segments.conversion_segments_size(), 1);
    const Segment& segment = segments.conversion_segment(0);
    ASSERT_EQ(segment.candidates_size(), 1) << test.raw;
    EXPECT_EQ(segment.candidate(0).value, test.raw);
  }
  Mock::VerifyAndClearExpectations(&dictionary);
}

TEST_F(LanguageAwareRewriterTest, KnownJapanesePrefixWithNoNativeCandidate) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);
  Segments segments;
  std::string composition;
  EXPECT_CALL(dictionary, HasKey(StrEq("もう"))).WillOnce(Return(true));
  EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "mouGoogle", false,
                                            &composition, &segments));
  EXPECT_EQ(composition, "もうGoogle");
  ASSERT_EQ(segments.conversion_segments_size(), 1);
  const Segment& segment = segments.conversion_segment(0);
  // The native predictor returned no candidates. Both safe, selectable
  // values must now be supplied by the language-aware rewriter itself.
  ASSERT_EQ(segment.candidates_size(), 2);
  EXPECT_EQ(segment.candidate(0).value, "もうGoogle");
  EXPECT_EQ(segment.candidate(1).value, "mouGoogle");
  EXPECT_EQ(segment.candidate(0).content_value, "もうGoogle");
  Mock::VerifyAndClearExpectations(&dictionary);
}

TEST_F(LanguageAwareRewriterTest,
       KnownJapanesePrefixWithUnrelatedNativeCandidate) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);
  Segments segments;
  PushFrontCandidate("もうグーグル", segments.add_segment());
  std::string composition;
  EXPECT_CALL(dictionary, HasKey(StrEq("もう"))).WillOnce(Return(true));
  EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "mouGoogle", false,
                                            &composition, &segments));
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 3);
  EXPECT_EQ(segment.candidate(0).value, "もうGoogle");
  EXPECT_EQ(segment.candidate(1).value, "mouGoogle");
  EXPECT_EQ(segment.candidate(2).value, "もうグーグル");
  Mock::VerifyAndClearExpectations(&dictionary);
}

TEST_F(LanguageAwareRewriterTest,
       ExistingMixedCandidateIsPromotedNotDuplicated) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);
  Segments segments;
  Segment* segment = segments.add_segment();
  PushFrontCandidate("もうGoogle", segment);
  PushFrontCandidate("もうグーグル", segment);
  std::string composition;
  EXPECT_CALL(dictionary, HasKey(StrEq("もう"))).WillOnce(Return(true));
  EXPECT_TRUE(RewriteWithLanguageAwareInput(&rewriter, "mouGoogle", false,
                                            &composition, &segments));
  ASSERT_EQ(segment->candidates_size(), 3);
  EXPECT_EQ(segment->candidate(0).value, "もうGoogle");
  EXPECT_EQ(segment->candidate(1).value, "mouGoogle");
  EXPECT_EQ(segment->candidate(2).value, "もうグーグル");
  Mock::VerifyAndClearExpectations(&dictionary);
}

TEST_F(LanguageAwareRewriterTest, ExplicitJapaneseReturnSuppressesRawRescue) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);
  commands::Request client_request;
  client_request.set_language_aware_input(
      commands::Request::LANGUAGE_AWARE_SUGGESTION);
  config::Config config;
  config.set_shift_key_mode_switch(config::Config::ASCII_INPUT_MODE);
  auto table = std::make_shared<composer::Table>();
  table->InitializeWithRequestAndConfig(client_request, config);
  composer::Composer composer(table, client_request, config);
  InsertASCIISequence("AI", &composer);
  ASSERT_EQ(composer.GetInputMode(), transliteration::HALF_ASCII);
  composer.SuppressLanguageAwareRawInput();
  composer.SetInputMode(transliteration::HIRAGANA);
  InsertASCIISequence("de", &composer);
  EXPECT_EQ(composer.GetRawString(), "AIde");
  EXPECT_TRUE(
      composer.CreateComposerData().suppress_language_aware_raw_input());

  Segments segments;
  segments.add_segment()->set_key(composer.GetQueryForPrediction());
  const ConversionRequest request =
      ConversionRequestBuilder()
          .SetComposer(composer)
          .SetRequest(client_request)
          .SetRequestType(ConversionRequest::SUGGESTION)
          .Build();
  EXPECT_FALSE(rewriter.Rewrite(request, &segments));
  EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 0);
  composer.Reset();
  EXPECT_FALSE(
      composer.CreateComposerData().suppress_language_aware_raw_input());
}

TEST_F(LanguageAwareRewriterTest, NotRewriteFullWidthAsciiToHalfWidthAscii) {
  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);
  {
    // "1d*=" is composed to "１ｄ＊＝", which are the full width ascii
    // characters of "1d*=". We do not want to rewrite full width ascii to
    // half width ascii by LanguageAwareRewriter.
    std::string composition;
    Segments segments;
    EXPECT_FALSE(RewriteWithLanguageAwareInput(
        &rewriter, "1d*=", false, /* is_mobile */ &composition, &segments));
    EXPECT_EQ(composition, "１ｄ＊＝");
  }
  {
    // "xyzw" is composed to "ｘｙｚｗ". Do not rewrite.
    std::string composition;
    Segments segments;
    EXPECT_FALSE(RewriteWithLanguageAwareInput(
        &rewriter, "xyzw", false, /* is_mobile */ &composition, &segments));
    EXPECT_EQ(composition, "ｘｙｚｗ");
  }
}

TEST_F(LanguageAwareRewriterTest, IsDisabledInTwelveKeyLayout) {
  struct {
    commands::Request::SpecialRomanjiTable table;
    config::Config::PreeditMethod preedit_method;
    int type;
  } const kParams[] = {
      // Enabled combinations.
      {commands::Request::DEFAULT_TABLE, config::Config::ROMAN,
       RewriterInterface::SUGGESTION | RewriterInterface::PREDICTION},
      {commands::Request::QWERTY_MOBILE_TO_HIRAGANA, config::Config::ROMAN,
       RewriterInterface::SUGGESTION | RewriterInterface::PREDICTION},
      // Disabled combinations.
      {commands::Request::DEFAULT_TABLE, config::Config::KANA,
       RewriterInterface::NOT_AVAILABLE},
      {commands::Request::TWELVE_KEYS_TO_HIRAGANA, config::Config::ROMAN,
       RewriterInterface::NOT_AVAILABLE},
      {commands::Request::TOGGLE_FLICK_TO_HIRAGANA, config::Config::ROMAN,
       RewriterInterface::NOT_AVAILABLE},
      {commands::Request::GODAN_TO_HIRAGANA, config::Config::ROMAN,
       RewriterInterface::NOT_AVAILABLE},
  };

  MockDictionary dictionary;
  LanguageAwareRewriter rewriter(PosMatcher(data_manager_.GetPosMatcherData()),
                                 dictionary);
  for (const auto& param : kParams) {
    commands::Request request;
    request.set_language_aware_input(
        commands::Request::LANGUAGE_AWARE_SUGGESTION);
    request.set_special_romanji_table(param.table);

    config::Config config;
    config.set_preedit_method(param.preedit_method);

    auto table = std::make_shared<composer::Table>();
    table->InitializeWithRequestAndConfig(request, config);

    composer::Composer composer(table, request, config);
    InsertASCIISequence("query", &composer);

    const ConversionRequest conv_request = ConversionRequestBuilder()
                                               .SetComposer(composer)
                                               .SetRequest(request)
                                               .SetConfig(config)
                                               .Build();

    EXPECT_EQ(rewriter.capability(conv_request), param.type);
  }
}

}  // namespace
}  // namespace mozc
