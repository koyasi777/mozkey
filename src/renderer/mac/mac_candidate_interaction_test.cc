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
#include "renderer/mac/mac_candidate_interaction.h"

#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "testing/gunit.h"

namespace mozc::renderer::mac {
namespace {

commands::CandidateWindow MakePassiveSuggestionWindow() {
  commands::CandidateWindow candidate_window;
  candidate_window.set_category(commands::SUGGESTION);
  auto* first = candidate_window.add_candidate();
  first->set_id(10);
  first->set_value("candidate-a");
  auto* second = candidate_window.add_candidate();
  second->set_id(20);
  second->set_value("candidate-b");
  return candidate_window;
}

TEST(MacCandidateInteractionTest, UnfocusedSuggestionIsPassive) {
  const auto candidate_window = MakePassiveSuggestionWindow();
  EXPECT_TRUE(IsPassiveSuggestionCandidateWindow(candidate_window));
}

TEST(MacCandidateInteractionTest, FocusedSuggestionIsNotPassive) {
  auto candidate_window = MakePassiveSuggestionWindow();
  candidate_window.set_focused_index(0);
  EXPECT_FALSE(IsPassiveSuggestionCandidateWindow(candidate_window));
}

TEST(MacCandidateInteractionTest, PredictionAndConversionAreNotPassive) {
  auto candidate_window = MakePassiveSuggestionWindow();
  candidate_window.set_category(commands::PREDICTION);
  EXPECT_FALSE(IsPassiveSuggestionCandidateWindow(candidate_window));
  candidate_window.set_category(commands::CONVERSION);
  candidate_window.set_focused_index(0);
  EXPECT_FALSE(IsPassiveSuggestionCandidateWindow(candidate_window));
}

TEST(MacCandidateInteractionTest, EmptySuggestionIsNotPassive) {
  commands::CandidateWindow candidate_window;
  candidate_window.set_category(commands::SUGGESTION);
  EXPECT_FALSE(IsPassiveSuggestionCandidateWindow(candidate_window));
}

TEST(MacCandidateInteractionTest, CapturesCandidateIdentityByIndex) {
  const auto candidate_window = MakePassiveSuggestionWindow();
  const auto identity =
      GetPassiveSuggestionCandidateIdentity(candidate_window, 1);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->id, 20);
  EXPECT_EQ(identity->value, "candidate-b");
  EXPECT_FALSE(
      GetPassiveSuggestionCandidateIdentity(candidate_window, -1).has_value());
  EXPECT_FALSE(
      GetPassiveSuggestionCandidateIdentity(candidate_window, 2).has_value());
}

TEST(MacCandidateInteractionTest, RejectsCandidateWithoutId) {
  commands::CandidateWindow candidate_window;
  candidate_window.set_category(commands::SUGGESTION);
  candidate_window.add_candidate()->set_value("candidate");
  EXPECT_FALSE(
      GetPassiveSuggestionCandidateIdentity(candidate_window, 0).has_value());
}

TEST(MacCandidateInteractionTest, IdentityMustKeepBothIdAndValue) {
  auto candidate_window = MakePassiveSuggestionWindow();
  const PassiveSuggestionCandidateIdentity original{
      .id = 10,
      .value = "candidate-a",
  };
  EXPECT_TRUE(
      ContainsPassiveSuggestionCandidateIdentity(candidate_window, original));

  candidate_window.mutable_candidate(0)->set_value("replacement");
  EXPECT_FALSE(
      ContainsPassiveSuggestionCandidateIdentity(candidate_window, original));

  candidate_window.mutable_candidate(0)->set_value("candidate-a");
  candidate_window.mutable_candidate(0)->set_id(99);
  EXPECT_FALSE(
      ContainsPassiveSuggestionCandidateIdentity(candidate_window, original));

  candidate_window.set_focused_index(0);
  EXPECT_FALSE(
      ContainsPassiveSuggestionCandidateIdentity(candidate_window, original));
}

}  // namespace
}  // namespace mozc::renderer::mac