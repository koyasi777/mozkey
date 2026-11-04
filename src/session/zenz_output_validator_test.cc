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

#include <string>

#include "testing/gunit.h"

namespace mozc::session {
namespace {

TEST(ZenzOutputValidatorTest, StrictReadingRejectsPartialSuffix) {
  const auto result = ZenzOutputValidator::ValidateReadingPreservation(
      "いっしょにいって", "いっしょにいって", "いって");
  EXPECT_FALSE(result.accept);
  EXPECT_EQ(result.reason, "reading_mismatch");
}

// Regression: the ordinary Mozc surface can itself cover less than the full
// typed key (e.g. baseline="行って" -> reading="いって").  Even then, strict
// mode must not accept the same shortened reading from a different Zenz value.
TEST(ZenzOutputValidatorTest, StrictReadingDoesNotTrustTruncatedMozcBaseline) {
  const auto result = ZenzOutputValidator::ValidateReadingPreservation(
      "いっしょにいって", "いっしょにいって", "いって");
  EXPECT_FALSE(result.accept);
  EXPECT_EQ(result.reason, "reading_mismatch");
}

TEST(ZenzOutputValidatorTest, StrictReadingAcceptsCompleteReading) {
  const auto result = ZenzOutputValidator::ValidateReadingPreservation(
      "いっしょにいって", "", "いっしょにいって");
  EXPECT_TRUE(result.accept);
  EXPECT_EQ(result.reason, "reading_preserved");
}

TEST(ZenzOutputValidatorTest, StrictReadingAcceptsNormalizedNumericReading) {
  const auto result = ZenzOutputValidator::ValidateReadingPreservation(
      "100えんではかえない", "ひゃくえんではかえない", "ひゃくえんではかえない");
  EXPECT_TRUE(result.accept);
}

TEST(ZenzOutputValidatorTest, StrictReadingRejectsUnavailableReading) {
  const auto result = ZenzOutputValidator::ValidateReadingPreservation(
      "かおもじ", "", "");
  EXPECT_FALSE(result.accept);
  EXPECT_EQ(result.reason, "reading_unavailable");
}

TEST(ZenzOutputValidatorTest, StrictReadingRejectsGeneratedExtraText) {
  const auto result = ZenzOutputValidator::ValidateReadingPreservation(
      "かぶ", "かぶ", "かぶしきがいしゃ");
  EXPECT_FALSE(result.accept);
  EXPECT_EQ(result.reason, "reading_mismatch");
}

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

TEST(ZenzOutputValidatorTest, DetectsObservedOmissionOfJapanesePrefix) {
  EXPECT_TRUE(ZenzOutputValidator::IsObviousSurfaceTruncation(
      "一緒に行って", "行って"));
  EXPECT_TRUE(ZenzOutputValidator::IsObviousSurfaceTruncation(
      "何とかなる", "なる"));
  EXPECT_TRUE(ZenzOutputValidator::IsObviousSurfaceTruncation(
      "なんとかなる", "なる"));
}

TEST(ZenzOutputValidatorTest, DoesNotRejectOrdinaryOrthographicCorrection) {
  EXPECT_FALSE(ZenzOutputValidator::IsObviousSurfaceTruncation(
      "彼は点滴です", "彼は天敵です"));
  EXPECT_FALSE(ZenzOutputValidator::IsObviousSurfaceTruncation(
      "100円では変えない", "100円では買えない"));
  EXPECT_FALSE(ZenzOutputValidator::IsObviousSurfaceTruncation(
      "行って", "一緒に行って"));
}

TEST(ZenzOutputValidatorTest, KeepsCompleteValueOnLeftContextOverlap) {
  const ZenzContextPrefixDecision decision =
      ZenzOutputValidator::ResolveLeftContextPrefix(
          "何とかなる", "何とか", "何とかなる");
  EXPECT_TRUE(decision.safe);
  EXPECT_FALSE(decision.stripped);
  EXPECT_EQ(decision.value, "何とかなる");
}

TEST(ZenzOutputValidatorTest, RejectsAmbiguousLeftContextPrefix) {
  const ZenzContextPrefixDecision decision =
      ZenzOutputValidator::ResolveLeftContextPrefix(
          "何とかなる", "何とか", "なんとかなる");
  EXPECT_FALSE(decision.safe);
  EXPECT_EQ(decision.value, "何とかなる");

  const ZenzContextPrefixDecision missing =
      ZenzOutputValidator::ResolveLeftContextPrefix(
          "病院に行って", "病院に", "一緒に行って");
  EXPECT_FALSE(missing.safe);
}

TEST(ZenzOutputValidatorTest, StripsOnlyExactUnambiguousContextEcho) {
  const ZenzContextPrefixDecision decision =
      ZenzOutputValidator::ResolveLeftContextPrefix(
          "病院に一緒に行って", "病院に", "一緒に行って");
  EXPECT_TRUE(decision.safe);
  EXPECT_TRUE(decision.stripped);
  EXPECT_EQ(decision.value, "一緒に行って");
}

TEST(ZenzOutputValidatorTest, KeepsNonEchoOrthographicCorrection) {
  const ZenzContextPrefixDecision decision =
      ZenzOutputValidator::ResolveLeftContextPrefix(
          "一緒に行って", "病院に", "いっしょにいって");
  EXPECT_TRUE(decision.safe);
  EXPECT_FALSE(decision.stripped);
  EXPECT_EQ(decision.value, "一緒に行って");
}

TEST(ZenzOutputValidatorTest, RejectsAmbiguousCorrectedPayloadAfterContext) {
  // The suffix can be an entirely valid correction, but without a reading
  // correspondence check we cannot prove that the prefix was context echo.
  const ZenzContextPrefixDecision decision =
      ZenzOutputValidator::ResolveLeftContextPrefix(
          "病院に一緒に行って", "病院に", "いっしょにいって");
  EXPECT_FALSE(decision.safe);
  EXPECT_FALSE(decision.stripped);
  EXPECT_EQ(decision.value, "病院に一緒に行って");
}


TEST(ZenzOutputValidatorTest, RejectsUnrequestedZenzEmojiByDefault) {
  ZenzValidationInput input;
  input.key = "えもじ";
  input.mozc_value = "絵文字";
  input.zenz_value = "🎉";
  input.min_key_length = 0;
  ZenzOutputValidator validator;
  const auto rejected = validator.Validate(input);
  EXPECT_FALSE(rejected.accept);
  EXPECT_EQ(rejected.reason, "emoji_sequence_changed");

  input.allow_zenz_emoji_changes = true;
  EXPECT_TRUE(validator.Validate(input).accept);
}

TEST(ZenzOutputValidatorTest, DetectsEmojiInsertionsAndReplacements) {
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences("クラッカー", "🎉"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "おめでとう", "おめでとう🎉"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "おめでとう🎉", "おめでとう😊"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "おめでとう🎉", "おめでとう"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "おめでとう🎉", "おめでとう🎉🎉"));
}

TEST(ZenzOutputValidatorTest, PreservesExplicitEmojiDuringJapaneseCorrection) {
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences(
      "今日は楽しい🎉", "今日は楽しかった🎉"));
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences(
      "😊ありがとう🎉", "😊ありがとうございます🎉"));
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences(
      "きょうはいいてんき", "今日はいい天気"));
}

TEST(ZenzOutputValidatorTest, TreatsEmojiClustersAsCompleteUnits) {
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences(
      "👩🏽‍💻で作業", "👩🏽‍💻として作業"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "👩🏽‍💻で作業", "👩🏻‍💻で作業"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "🇯🇵です", "🇰🇷です"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "数字1️⃣です", "数字2️⃣です"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "数字1⃣です", "数字2⃣です"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "ありがとう", "ありがとう❤️"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "ありがとう", "ありがとう❤"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences(
      "お知らせ", "お知らせ⚠"));
  // Plain trademark/copyright signs are ordinary text unless VS16 is used.
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences("商品", "商品©"));
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences("商品", "商品©️"));
}

// A non-emoji prefix followed by ZWJ can be merged with a real emoji by
// Mozc's simplified grapheme splitter. Do not lose the trailing emoji.
TEST(ZenzOutputValidatorTest, DetectsEmojiAfterNonEmojiJoiner) {
  const std::string joined = std::string("A\xE2\x80\x8D") +
                             "\xF0\x9F\x8E\x89";
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences("A", joined));
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences(joined, joined));
  // The standalone keycap mark can be split from a base attached to ZWJ.
  const std::string keycap = std::string("A\xE2\x80\x8D") + "1" +
                             "\xE2\x83\xA3";
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences("A", keycap));
}

// VS15 requests text presentation for only the preceding character. It must
// not hide a later emoji in the same imperfectly split ZWJ cluster.
TEST(ZenzOutputValidatorTest, TextSelectorDoesNotHideFollowingEmoji) {
  const std::string joined =
      std::string("\xE2\x9A\xA0\xEF\xB8\x8E\xE2\x80\x8D") +
      "\xF0\x9F\x8E\x89";
  EXPECT_FALSE(ZenzOutputValidator::PreservesEmojiSequences("plain", joined));
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences(joined, joined));
}

// VS16 alone does not turn an unrelated ASCII character into emoji.
TEST(ZenzOutputValidatorTest, IgnoresUnrelatedPresentationSelector) {
  const std::string stylized = "A\xEF\xB8\x8F";
  EXPECT_TRUE(ZenzOutputValidator::PreservesEmojiSequences("A", stylized));
}

TEST(ZenzOutputValidatorTest, BlocksDeletionOfExplicitlyRepeatedInput) {
  // A normal IME does not silently correct a user's explicitly typed repeat.
  EXPECT_TRUE(ZenzOutputValidator::IsObviousSurfaceTruncation(
      "よろしくお願いしますお願いします", "よろしくお願いします"));
}

}  // namespace
}  // namespace mozc::session
