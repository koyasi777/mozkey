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

#include <memory>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "composer/composer.h"
#include "converter/converter_interface.h"
#include "converter/segments.h"
#include "engine/engine.h"
#include "engine/engine_factory.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

ConversionRequest ConvReq(absl::string_view key) {
  composer::Composer composer;
  composer.SetPreeditTextForTestOnly(key);
  return ConversionRequestBuilder().SetComposer(composer).Build();
}

class ConverterRegressionTest : public testing::TestWithTempUserProfile {};

TEST_F(ConverterRegressionTest, QueryOfDeathTest) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  std::shared_ptr<const ConverterInterface> converter = engine->GetConverter();

  CHECK(converter);
  {
    Segments segments;
    EXPECT_TRUE(
        converter->StartConversion(ConvReq("りゅきゅけmぽ"), &segments));
  }
  {
    Segments segments;
    EXPECT_TRUE(converter->StartConversion(ConvReq("5.1,||t:1"), &segments));
  }
  {
    Segments segments;
    // Converter returns false, but not crash.
    EXPECT_FALSE(converter->StartConversion(ConvReq(""), &segments));
  }
  {
    Segments segments;
    const ConversionRequest conv_request;
    // Converter returns false, but not crash.
    EXPECT_FALSE(converter->StartConversion(conv_request, &segments));
  }
}

TEST_F(ConverterRegressionTest, Regression3323108) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  std::shared_ptr<const ConverterInterface> converter = engine->GetConverter();
  Segments segments;

  EXPECT_TRUE(
      converter->StartConversion(ConvReq("ここではきものをぬぐ"), &segments));
  EXPECT_EQ(segments.conversion_segments_size(), 3);
  const ConversionRequest default_request;
  EXPECT_TRUE(converter->ResizeSegment(&segments, default_request, 1, 2));
  EXPECT_EQ(segments.conversion_segments_size(), 2);
  EXPECT_EQ(segments.conversion_segment(1).key(), "きものをぬぐ");
}

// Verify a single conversion segment without an explicit ResizeSegments call.
// This tests both the default segmenter decision and the top surface.
TEST_F(ConverterRegressionTest, KizukeruIsOneSegmentWithoutManualResize) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  ASSERT_TRUE(engine);
  std::shared_ptr<const ConverterInterface> converter = engine->GetConverter();
  ASSERT_TRUE(converter);

  Segments segments;
  ASSERT_TRUE(converter->StartConversion(ConvReq("きづける"), &segments));
  ASSERT_EQ(segments.conversion_segments_size(), 1) << segments.DebugString();
  const Segment& segment = segments.conversion_segment(0);
  EXPECT_EQ(segment.key(), "きづける");
  ASSERT_GT(segment.candidates_size(), 0);
  EXPECT_EQ(segment.candidate(0).value, "気付ける");
}

TEST_F(ConverterRegressionTest, HyphenlessZipcodeConversion) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  std::shared_ptr<const ConverterInterface> converter = engine->GetConverter();
  ASSERT_TRUE(converter);

  config::Config config;
  config.set_use_zip_code_conversion(true);
  config.set_space_character_form(config::Config::FUNDAMENTAL_HALF_WIDTH);

  for (const absl::string_view input : {"1000000", "１００００００"}) {
    composer::Composer composer;
    composer.SetPreeditTextForTestOnly(input);
    const ConversionRequest request = ConversionRequestBuilder()
                                          .SetComposer(composer)
                                          .SetConfig(config)
                                          .Build();
    EXPECT_EQ(request.key(), "1000000");
    EXPECT_TRUE(request.options().enable_hyphenless_zipcode_alias);

    Segments segments;
    ASSERT_TRUE(converter->StartConversion(request, &segments)) << input;

    bool found_address = false;
    bool found_combined = false;
    for (const Segment& segment : segments.conversion_segments()) {
      for (const auto& candidate : segment.candidates()) {
        if (candidate->key == "1000000" &&
            candidate->value == "東京都千代田区") {
          found_address = true;
        }
        if (candidate->key == "1000000" &&
            candidate->description == "郵便番号と住所" &&
            candidate->value == "1000000 東京都千代田区") {
          found_combined = true;
        }
      }
    }
    EXPECT_TRUE(found_address) << input;
    EXPECT_TRUE(found_combined) << input;
  }
}

TEST_F(ConverterRegressionTest, HyphenlessZipcodeInExplicitSentence) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  std::shared_ptr<const ConverterInterface> converter = engine->GetConverter();
  ASSERT_TRUE(converter);
  config::Config config;
  config.set_use_zip_code_conversion(true);

  composer::Composer composer;
  composer.SetPreeditTextForTestOnly("きょうは1000000です");
  const ConversionRequest request = ConversionRequestBuilder()
                                        .SetComposer(composer)
                                        .SetConfig(config)
                                        .Build();
  ASSERT_TRUE(request.options().enable_hyphenless_zipcode_alias);
  Segments segments;
  ASSERT_TRUE(converter->StartConversion(request, &segments));
  bool found_postal_address = false;
  for (const Segment& segment : segments.conversion_segments()) {
    for (const converter::Candidate* candidate : segment.candidates()) {
      if (candidate->key == "1000000" &&
          candidate->value == "東京都千代田区") {
        found_postal_address = true;
      }
    }
  }
  EXPECT_TRUE(found_postal_address) << segments.DebugString();
}

// An alias at an internal lattice offset must respect the preceding ASCII
// letter, and prefix lookup must respect the following ASCII letter.
TEST_F(ConverterRegressionTest, HyphenlessZipcodeRejectsAsciiIdentifiers) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  std::shared_ptr<const ConverterInterface> converter = engine->GetConverter();
  ASSERT_TRUE(converter);
  config::Config config;
  config.set_use_zip_code_conversion(true);

  for (const absl::string_view input : {
           "ID1000000", "1000000ABC", "x1000000y", "1231000000"}) {
    SCOPED_TRACE(input);
    composer::Composer composer;
    composer.SetPreeditTextForTestOnly(input);
    const ConversionRequest request = ConversionRequestBuilder()
                                          .SetComposer(composer)
                                          .SetConfig(config)
                                          .Build();
    ASSERT_TRUE(request.options().enable_hyphenless_zipcode_alias);
    Segments segments;
    ASSERT_TRUE(converter->StartConversion(request, &segments));
    for (const Segment& segment : segments.conversion_segments()) {
      for (const auto& candidate : segment.candidates()) {
        EXPECT_FALSE(candidate->key == "1000000" &&
                     candidate->value == "東京都千代田区")
            << input << ": " << segments.DebugString();
      }
    }
  }
}

TEST_F(ConverterRegressionTest, HyphenlessZipcodeAliasRequestScope) {
  config::Config config;
  config.set_use_zip_code_conversion(true);
  for (const absl::string_view input : {
           "10000000", "1231000000", "1000000です", "きょうは1000000", "100-0000"}) {
    composer::Composer composer;
    composer.SetPreeditTextForTestOnly(input);
    const ConversionRequest request = ConversionRequestBuilder()
                                          .SetComposer(composer)
                                          .SetConfig(config)
                                          .Build();
    EXPECT_TRUE(request.options().enable_hyphenless_zipcode_alias) << input;
  }
  composer::Composer composer;
  composer.SetPreeditTextForTestOnly("1000000");
  const ConversionRequest suggestion = ConversionRequestBuilder()
                                           .SetComposer(composer)
                                           .SetConfig(config)
                                           .SetRequestType(RequestType::SUGGESTION)
                                           .Build();
  EXPECT_FALSE(suggestion.options().enable_hyphenless_zipcode_alias);
  ConversionOptions realtime_options;
  realtime_options.used_in_predictor_realtime_conversion = true;
  const ConversionRequest realtime = ConversionRequestBuilder()
                                         .SetComposer(composer)
                                         .SetConfig(config)
                                         .SetOptions(realtime_options)
                                         .Build();
  EXPECT_FALSE(realtime.options().enable_hyphenless_zipcode_alias);
}

// Real production dictionary regression.  Unlike a synthetic reranker
// fixture, this covers lattice segmentation, N-best candidates, and the
// EngineConverter's actual converted preedit surface.
TEST_F(ConverterRegressionTest,
       NiParticleAfterCommittedLeftContextBeforeDemonstrative) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  ASSERT_TRUE(engine);
  for (absl::string_view left : {"()", "（管理者）", "API", "API "}) {
    SCOPED_TRACE(std::string(left));
    auto converter = engine->CreateEngineConverter();
    ASSERT_TRUE(converter);
    composer::Composer composer;
    composer.SetPreeditTextForTestOnly("にそのないようを");
    commands::Context context;
    context.set_preceding_text(std::string(left));
    ASSERT_TRUE(converter->Convert(composer, context));
    commands::Output output;
    converter->FillOutput(composer, &output);
    ASSERT_GE(output.preedit().segment_size(), 3) << output.DebugString();
    EXPECT_EQ(output.preedit().segment(0).value(), "に")
        << output.DebugString();
    EXPECT_EQ(output.preedit().segment(1).value(), "その")
        << output.DebugString();
    EXPECT_EQ(output.preedit().segment(2).value(), "内容を")
        << output.DebugString();
  }
}

TEST_F(ConverterRegressionTest,
       NiParticleDoesNotOverrideNumericShikaAfterTopicContext) {
  std::unique_ptr<Engine> engine = EngineFactory::Create().value();
  ASSERT_TRUE(engine);
  auto converter = engine->CreateEngineConverter();
  composer::Composer composer;
  composer.SetPreeditTextForTestOnly("にしかない");
  commands::Context context;
  context.set_preceding_text("候補は");
  ASSERT_TRUE(converter->Convert(composer, context));
  commands::Output output;
  converter->FillOutput(composer, &output);
  ASSERT_GE(output.preedit().segment_size(), 1);
  EXPECT_EQ(output.preedit().segment(0).value(), "二しか")
      << output.DebugString();
}

}  // namespace
}  // namespace mozc
