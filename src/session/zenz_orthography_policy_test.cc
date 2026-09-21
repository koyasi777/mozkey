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

#include "session/zenz_orthography_policy.h"

#include "testing/gunit.h"

namespace mozc::session {
namespace {

TEST(ZenzOrthographyPolicyTest, RejectsJapaneseSurfaceRomanization) {
  ZenzOrthographyPolicy policy;

  const ZenzOrthographyDecision decision = policy.Evaluate("東京", "Tokyo");

  EXPECT_FALSE(decision.allow);
  EXPECT_EQ(decision.reason, "alphabetic_surface_changed");
}

TEST(ZenzOrthographyPolicyTest, AllowsExistingLatinSurface) {
  ZenzOrthographyPolicy policy;

  EXPECT_TRUE(policy.Evaluate("GitHubを使う", "GitHubを使用する").allow);
  EXPECT_TRUE(policy.Evaluate("AIについて", "AIに関して").allow);
  EXPECT_TRUE(policy.Evaluate("Tokyoにいく", "Tokyoに行く").allow);
}

TEST(ZenzOrthographyPolicyTest, RejectsMutationOfExistingLatinSurface) {
  ZenzOrthographyPolicy policy;

  EXPECT_FALSE(policy.Evaluate("GitHubを使う", "GitLabを使う").allow);
  EXPECT_FALSE(policy.Evaluate("Tokyoに行く", "TOKYOに行く").allow);
}

TEST(ZenzOrthographyPolicyTest, RejectsTechnicalTokenMutation) {
  ZenzOrthographyPolicy policy;

  EXPECT_FALSE(policy.Evaluate("C++で書く", "C#で書く").allow);
  EXPECT_FALSE(policy.Evaluate("GPT-5を使う", "GPT5を使う").allow);
  EXPECT_FALSE(policy.Evaluate("UTF-8で保存", "UTF8で保存").allow);
  EXPECT_FALSE(policy.Evaluate("HTTP/2で接続", "HTTP/3で接続").allow);
  EXPECT_FALSE(policy.Evaluate("Windows11を使う", "Windows12を使う").allow);
}

TEST(ZenzOrthographyPolicyTest, DoesNotFreezeUnrelatedNumbers) {
  ZenzOrthographyPolicy policy;

  EXPECT_TRUE(policy.Evaluate("AIを2回使う", "AIを3回使う").allow);
}

TEST(ZenzOrthographyPolicyTest, RejectsRemovalOfExistingLatinSurface) {
  ZenzOrthographyPolicy policy;

  EXPECT_FALSE(policy.Evaluate("GitHubを使う", "ギットハブを使う").allow);
}

TEST(ZenzOrthographyPolicyTest, RejectsAdditionalAlphabetOccurrence) {
  ZenzOrthographyPolicy policy;

  EXPECT_FALSE(policy.Evaluate("AIを使う", "AIとAIを使う").allow);
}

TEST(ZenzOrthographyPolicyTest, AllowsJapaneseOnlyRewrite) {
  ZenzOrthographyPolicy policy;

  EXPECT_TRUE(policy.Evaluate("彼は点滴です", "彼は天敵です").allow);
}

}  // namespace
}  // namespace mozc::session
