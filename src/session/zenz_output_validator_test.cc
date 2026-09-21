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

#include "session/zenz_output_validator.h"

#include "testing/gunit.h"

namespace mozc::session {
namespace {

TEST(ZenzOutputValidatorTest, RestoreUserVisibleSymbolStyleFullwidthBrackets) {
  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "これは（てすと）です", "これは（テスト）です",
                "これは(テスト)だと思います"),
            "これは（テスト）だと思います");
}

TEST(ZenzOutputValidatorTest, RestoreUserVisibleSymbolStyleAsciiBrackets) {
  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "これは(test)です", "これは(test)です",
                "これは（test）です"),
            "これは(test)です");
}

TEST(ZenzOutputValidatorTest, RestoreUserVisibleSymbolStyleMixedBrackets) {
  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "（A）と(B)", "（A）と(B)", "(A)と(B)"),
            "（A）と(B)");
}

TEST(ZenzOutputValidatorTest, RestoreUserVisibleSymbolStyleQuestionAndBang) {
  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "本当ですか？！", "本当ですか？！", "本当ですか?!"),
            "本当ですか？！");

  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "OK?!", "OK?!", "OK？！"),
            "OK?!");
}

TEST(ZenzOutputValidatorTest, RestoreUserVisibleSymbolStyleKeepsWaveDash) {
  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "ほ〜", "ほ〜", "ほ~"),
            "ほ〜");

  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "ほ~", "ほ~", "ほ〜"),
            "ほ~");
}

TEST(ZenzOutputValidatorTest, RestoreUserVisibleSymbolStyleFullwidthAsciiPunct) {
  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "注：A；B，C．", "注：A；B，C．", "注:A;B,C."),
            "注：A；B，C．");

  EXPECT_EQ(ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
                "note:A;B,C.", "note:A;B,C.", "note：A；B，C．"),
            "note:A;B,C.");
}

TEST(ZenzOutputValidatorTest,
     RepairUserControlledSymbolsSuppressesContextCopiedExclamation) {
  // Regression for an observed raw scorer response:
  //   left_context = "彼を天敵にするな！"
  //   reading      = "スルナ"
  //   raw Zenz     = "するな！"
  // With no requested trailing punctuation in the current key/Mozc value,
  // the IME boundary must keep the exclamation mark under user control.
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "するな", "するな", "するな！"),
            "するな");

  // The same left context was also observed to produce "やるな！" from
  // reading "ヤルナ"; this must follow the same output contract.
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "やるな", "やるな", "やるな！"),
            "やるな");
}

TEST(ZenzOutputValidatorTest,
     RepairUserControlledSymbolsSuppressesUnrequestedSentenceFinalPunctuation) {
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "するな", "するな", "するな!"),
            "するな");
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "やるな", "やるな", "やるな！"),
            "やるな");
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "ほんとうですか", "本当ですか", "本当ですか？"),
            "本当ですか");
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "きょうはあめ", "今日は雨", "今日は雨。"),
            "今日は雨");
}

TEST(ZenzOutputValidatorTest,
     RepairUserControlledSymbolsPreservesSemanticCorrection) {
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "あしたはあめ", "明日は飴", "明日は雨!"),
            "明日は雨");
}

TEST(ZenzOutputValidatorTest,
     RepairUserControlledSymbolsPreservesRequestedPunctuationAndStyle) {
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "するな！", "するな！", "するな!!"),
            "するな！");
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "本当ですか？", "本当ですか？", "本当ですか?!"),
            "本当ですか？");
}

TEST(ZenzOutputValidatorTest,
     RepairUserControlledSymbolsLeavesInteriorLexicalPunctuationUntouched) {
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "さんてんいちよん", "三点一四", "3.14"),
            "3.14");

  // Interior punctuation in the key/Mozc value must not authorize punctuation
  // at the end.  The period in "3.14" is lexical/technical content, so the
  // added trailing period in "3.14." is still unrequested and is removed.
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "さんてんいちよん", "3.14", "3.14."),
            "3.14");
}

TEST(ZenzOutputValidatorTest,
     RepairUserControlledSymbolsSuppressesUnrequestedExpressiveSuffix) {
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "すごい", "すごい", "すごい〜"),
            "すごい");
  EXPECT_EQ(ZenzOutputValidator::RepairUserControlledSymbols(
                "そうなんだ", "そうなんだ", "そうなんだ……"),
            "そうなんだ");
}

TEST(ZenzOutputValidatorTest, RejectsObservedLeftContextEcho) {
  ZenzValidationInput input;
  input.key = "おな";
  input.mozc_value = "同じ";
  input.zenz_value = "同じ試験を同じ試験と同じ";
  input.left_context = "現状は同じ試験を";
  input.min_key_length = 2;
  input.allow_synthetic_candidate = true;

  const ZenzValidationResult result = ZenzOutputValidator().Validate(input);
  EXPECT_FALSE(result.accept);
  EXPECT_FALSE(result.synthetic);
  EXPECT_EQ(result.reason, "left_context_echo");
}

TEST(ZenzOutputValidatorTest, RejectsPureLongContextSuffixEcho) {
  ZenzValidationInput input;
  input.key = "おな";
  input.mozc_value = "同じ";
  input.zenz_value = "同じ試験を";
  input.left_context = "現状は同じ試験を";
  input.min_key_length = 2;
  input.allow_synthetic_candidate = true;

  const ZenzValidationResult result = ZenzOutputValidator().Validate(input);
  EXPECT_FALSE(result.accept);
  EXPECT_EQ(result.reason, "left_context_echo");
}

TEST(ZenzOutputValidatorTest, RejectsContextSuffixPlusCurrentConversion) {
  ZenzValidationInput input;
  input.key = "おな";
  input.mozc_value = "同じ";
  input.zenz_value = "同じ試験を同じ";
  input.left_context = "現状は同じ試験を";
  input.min_key_length = 2;
  input.allow_synthetic_candidate = true;

  const ZenzValidationResult result = ZenzOutputValidator().Validate(input);
  EXPECT_FALSE(result.accept);
  EXPECT_EQ(result.reason, "left_context_echo");
}

TEST(ZenzOutputValidatorTest,
     AllowsRepeatedPhraseWhenCurrentReadingSupportsWholePhrase) {
  ZenzValidationInput input;
  input.key = "おなじしけんを";
  input.mozc_value = "同じ試験";
  input.zenz_value = "同じ試験を";
  input.left_context = "現状は同じ試験を";
  input.min_key_length = 2;
  input.allow_synthetic_candidate = true;

  const ZenzValidationResult result = ZenzOutputValidator().Validate(input);
  EXPECT_TRUE(result.accept);
  EXPECT_TRUE(result.synthetic);
  EXPECT_EQ(result.reason, "accepted_synthetic");
}

TEST(ZenzOutputValidatorTest, AllowsShortReadingAbbreviationMatchingContext) {
  ZenzValidationInput input;
  input.key = "かぶ";
  input.mozc_value = "株";
  input.zenz_value = "株式会社";
  input.left_context = "前回は株式会社";
  input.min_key_length = 2;
  input.allow_synthetic_candidate = true;

  const ZenzValidationResult result = ZenzOutputValidator().Validate(input);
  EXPECT_TRUE(result.accept);
  EXPECT_TRUE(result.synthetic);
  EXPECT_EQ(result.reason, "accepted_synthetic");
}

}  // namespace
}  // namespace mozc::session
