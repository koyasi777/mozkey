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

#include "rewriter/command_rewriter.h"

#include <cstddef>
#include <initializer_list>
#include <string>

#include "config/config_handler.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

size_t CommandCandidatesSize(const Segment& segment) {
  size_t result = 0;
  for (int i = 0; i < segment.candidates_size(); ++i) {
    if (segment.candidate(i).attributes &
        converter::Attribute::COMMAND_CANDIDATE) {
      result++;
    }
  }
  return result;
}

std::string GetCommandCandidateValue(const Segment& segment) {
  for (int i = 0; i < segment.candidates_size(); ++i) {
    if (segment.candidate(i).attributes &
        converter::Attribute::COMMAND_CANDIDATE) {
      return segment.candidate(i).value;
    }
  }
  return "";
}

class CommandRewriterTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    config::ConfigHandler::GetDefaultConfig(&config_);
    request_.Clear();
  }

  void TearDown() override {
    config::ConfigHandler::GetDefaultConfig(&config_);
    request_.Clear();
  }

  static ConversionRequest ConvReq(const config::Config& config,
                                   const commands::Request& request) {
    return ConversionRequestBuilder()
        .SetConfig(config)
        .SetRequest(request)
        .Build();
  }

  commands::Request request_;
  config::Config config_;
};

TEST_F(CommandRewriterTest, Rewrite) {
  CommandRewriter rewriter;
  Segments segments;
  Segment* seg = segments.push_back_segment();

  config_.set_use_history_suggest(true);
  const ConversionRequest convreq = ConvReq(config_, request_);
  EXPECT_FALSE(rewriter.Rewrite(convreq, &segments));

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("こまんど");
    candidate->value = "コマンド";
    EXPECT_TRUE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(CommandCandidatesSize(*seg), 4);
    seg->clear_candidates();
  }

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("さじぇすと");
    candidate->value = "サジェスト";
    EXPECT_TRUE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(CommandCandidatesSize(*seg), 1);
    seg->clear_candidates();
  }

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("ひみつ");
    candidate->value = "秘密";
    EXPECT_TRUE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(CommandCandidatesSize(*seg), 1);
    seg->clear_candidates();
  }

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("きょうと");
    candidate->value = "京都";
    EXPECT_FALSE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(CommandCandidatesSize(*seg), 0);
    seg->clear_candidates();
  }

  {
    // don't trigger when multiple segments.
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("こまんど");
    candidate->value = "コマンド";
    Segment* seg2 = segments.push_back_segment();
    converter::Candidate* candidate2 = seg2->add_candidate();
    seg2->set_key("です");
    candidate2->value = "です";
    EXPECT_FALSE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(CommandCandidatesSize(*seg), 0);
  }
}

TEST_F(CommandRewriterTest, LiveCommandCandidates) {
  using Candidate = converter::Candidate;
  CommandRewriter rewriter;
  for (const bool live : {false, true}) {
    for (const bool zenz : {false, true}) {
      for (const bool suggest : {false, true}) {
        for (const size_t count : {size_t{1}, size_t{9}}) {
          SCOPED_TRACE(::testing::Message()
                       << "live=" << live << " zenz=" << zenz
                       << " suggest=" << suggest << " count=" << count);
          config_.set_use_live_conversion(live);
          config_.set_use_zenz_live_correction(zenz);
          config_.set_use_history_suggest(suggest);
          config_.set_use_dictionary_suggest(false);
          config_.set_use_realtime_conversion(false);
          config_.set_incognito_mode(false);
          config_.set_presentation_mode(false);
          Segments segments;
          Segment* seg = segments.push_back_segment();
          seg->set_key("こまんど");
          for (size_t i = 1; i < count; ++i) {
            seg->add_candidate()->value = std::to_string(i);
          }
          Candidate* reference = seg->add_candidate();
          reference->key = "こまんど";
          reference->content_key = reference->key;
          reference->value = "コマンド";
          reference->content_value = reference->value;
          reference->cost = 1234;
          const size_t first = count < 6 ? count : 6;
          const size_t command_count = suggest ? 4 : 3;
          ASSERT_TRUE(rewriter.Rewrite(ConvReq(config_, request_), &segments));
          ASSERT_EQ(CommandCandidatesSize(*seg), command_count);
          ASSERT_EQ(seg->candidates_size(), count + command_count);
          EXPECT_EQ(seg->candidate(first).command,
                    Candidate::ENABLE_INCOGNITO_MODE);
          if (suggest) {
            EXPECT_EQ(seg->candidate(first + 1).command,
                      Candidate::ENABLE_PRESENTATION_MODE);
          }
          const Candidate& live_candidate =
              seg->candidate(first + command_count - 2);
          const Candidate& zenz_candidate =
              seg->candidate(first + command_count - 1);
          EXPECT_EQ(live_candidate.value,
                    live ? "ライブ変換をオフ" : "ライブ変換をオン");
          EXPECT_EQ(live_candidate.command,
                    live ? Candidate::DISABLE_LIVE_CONVERSION
                         : Candidate::ENABLE_LIVE_CONVERSION);
          EXPECT_EQ(zenz_candidate.value,
                    zenz ? "Zenzライブ補正をオフ" : "Zenzライブ補正をオン");
          EXPECT_EQ(zenz_candidate.command,
                    zenz ? Candidate::DISABLE_ZENZ_LIVE_CORRECTION
                         : Candidate::ENABLE_ZENZ_LIVE_CORRECTION);
          for (size_t i = first; i < first + command_count; ++i) {
            const Candidate& candidate = seg->candidate(i);
            EXPECT_TRUE(candidate.attributes &
                        converter::Attribute::COMMAND_CANDIDATE);
            EXPECT_TRUE(candidate.attributes & converter::Attribute::NO_LEARNING);
            EXPECT_EQ(candidate.key, "こまんど");
            EXPECT_EQ(candidate.content_key, "こまんど");
            EXPECT_EQ(candidate.cost, 1234);
            EXPECT_EQ(candidate.value, candidate.content_value);
            EXPECT_EQ(candidate.prefix, "【");
            EXPECT_EQ(candidate.suffix, "】");
            EXPECT_EQ(candidate.description, "設定を変更します");
            EXPECT_TRUE(candidate.inner_segment_boundary.empty());
          }
          EXPECT_EQ(reference->value, "コマンド");
          EXPECT_EQ(reference->command, Candidate::DEFAULT_COMMAND);
        }
      }
    }
  }
}

TEST_F(CommandRewriterTest, ValueCheck) {
  CommandRewriter rewriter;
  Segments segments;
  Segment* seg = segments.push_back_segment();

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("さじぇすと");
    candidate->value = "サジェスト";
    config_.set_presentation_mode(false);
    const ConversionRequest convreq = ConvReq(config_, request_);
    EXPECT_TRUE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(GetCommandCandidateValue(*seg), "サジェスト機能の一時停止");
    seg->clear_candidates();
  }

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("さじぇすと");
    candidate->value = "サジェスト";
    config_.set_presentation_mode(true);
    const ConversionRequest convreq = ConvReq(config_, request_);
    EXPECT_TRUE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(GetCommandCandidateValue(*seg), "サジェスト機能を元に戻す");
    seg->clear_candidates();
  }

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("ひみつ");
    candidate->value = "秘密";
    config_.set_incognito_mode(false);
    const ConversionRequest convreq = ConvReq(config_, request_);
    EXPECT_TRUE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(GetCommandCandidateValue(*seg), "シークレットモードをオン");
    seg->clear_candidates();
  }

  {
    converter::Candidate* candidate = seg->add_candidate();
    seg->set_key("ひみつ");
    candidate->value = "秘密";
    config_.set_incognito_mode(true);
    const ConversionRequest convreq = ConvReq(config_, request_);
    EXPECT_TRUE(rewriter.Rewrite(convreq, &segments));
    EXPECT_EQ(GetCommandCandidateValue(*seg), "シークレットモードをオフ");
    seg->clear_candidates();
  }
}

}  // namespace
}  // namespace mozc
