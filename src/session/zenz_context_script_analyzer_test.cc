#include "session/zenz_context_script_analyzer.h"

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzContextScriptAnalyzerTest,
     RecognizesJapaneseScriptsByUnicodeCodepoint) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("あア漢");

  EXPECT_EQ(profile.total_chars, 3);
  EXPECT_EQ(profile.japanese_script_chars, 3);
  EXPECT_EQ(profile.non_japanese_alnum_chars, 0);
  EXPECT_EQ(profile.emoji_chars, 0);
}

TEST(ZenzContextScriptAnalyzerTest,
     EmojiDoesNotCountAsJapaneseScriptSignal) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("😀");

  EXPECT_EQ(profile.total_chars, 1);
  EXPECT_EQ(profile.japanese_script_chars, 0);
  EXPECT_EQ(profile.emoji_chars, 1);
  EXPECT_FALSE(analyzer.LooksMostlyJapanese(profile));
  EXPECT_EQ(
      analyzer.ClassifyForContextClass(profile),
      "symbol_or_other");
}

TEST(ZenzContextScriptAnalyzerTest,
     FullWidthLatinAndNumberDoNotCountAsJapaneseScript) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("Ａ１");

  EXPECT_EQ(profile.total_chars, 2);
  EXPECT_EQ(profile.japanese_script_chars, 0);
  EXPECT_EQ(profile.non_japanese_alnum_chars, 2);
  EXPECT_FALSE(analyzer.LooksMostlyJapanese(profile));
  EXPECT_EQ(
      analyzer.ClassifyForContextClass(profile),
      "ascii_or_digit");
}

TEST(ZenzContextScriptAnalyzerTest,
     HanCharactersRemainJapaneseScriptSignal) {
  const ZenzContextScriptAnalyzer analyzer;

  // Unicode Han ideographs are shared by Japanese and Chinese. At this layer
  // they intentionally remain KANJI script signal rather than attempting
  // unreliable language identification from isolated context.
  const ZenzContextScriptProfile profile =
      analyzer.Analyze("中文");

  EXPECT_EQ(profile.total_chars, 2);
  EXPECT_EQ(profile.japanese_script_chars, 2);
  EXPECT_TRUE(analyzer.LooksMostlyJapanese(profile));
  EXPECT_EQ(
      analyzer.ClassifyForContextClass(profile),
      "japanese_only");
}

TEST(ZenzContextScriptAnalyzerTest,
     PreservesLegacyClassVocabularyForJapaneseContext) {
  const ZenzContextScriptAnalyzer analyzer;

  EXPECT_EQ(
      analyzer.ClassifyForContextClass(
          analyzer.Analyze("日本語")),
      "japanese_only");

  EXPECT_EQ(
      analyzer.ClassifyForContextClass(
          analyzer.Analyze("日本語?")),
      "japanese_with_punctuation");

  EXPECT_EQ(
      analyzer.ClassifyForContextClass(
          analyzer.Analyze("日本語A")),
      "mixed_japanese_ascii");

  EXPECT_EQ(
      analyzer.ClassifyForContextClass(
          analyzer.Analyze("ABC")),
      "ascii_or_digit");
}

TEST(ZenzContextScriptAnalyzerTest,
     JapaneseWithEmojiKeepsJapaneseSignalWithoutEmojiInflation) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("日本😀");

  EXPECT_EQ(profile.japanese_script_chars, 2);
  EXPECT_EQ(profile.emoji_chars, 1);
  EXPECT_TRUE(analyzer.LooksMostlyJapanese(profile));

  // Keep the existing persistent feedback vocabulary in Phase C1.
  EXPECT_EQ(
      analyzer.ClassifyForContextClass(profile),
      "japanese_only");
}

TEST(ZenzContextScriptAnalyzerTest,
     PreservesPreviousMixedAsciiAcceptanceEnvelope) {
  const ZenzContextScriptAnalyzer analyzer;

  {
    const ZenzContextScriptProfile profile =
        analyzer.Analyze("今日はABC");

    EXPECT_EQ(profile.japanese_script_chars, 3);
    EXPECT_EQ(profile.non_japanese_alnum_chars, 3);
    EXPECT_TRUE(analyzer.LooksMostlyJapanese(profile));
  }

  {
    const ZenzContextScriptProfile profile =
        analyzer.Analyze("日ABC");

    EXPECT_EQ(profile.japanese_script_chars, 1);
    EXPECT_EQ(profile.non_japanese_alnum_chars, 3);
    EXPECT_FALSE(analyzer.LooksMostlyJapanese(profile));
  }

  {
    const ZenzContextScriptProfile profile =
        analyzer.Analyze("日本語ABCDE");

    EXPECT_EQ(profile.japanese_script_chars, 3);
    EXPECT_EQ(profile.non_japanese_alnum_chars, 5);
    EXPECT_FALSE(analyzer.LooksMostlyJapanese(profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     EmojiCannotIncreaseJapaneseDominanceAgainstAscii) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("日😀????");

  EXPECT_EQ(profile.japanese_script_chars, 1);
  EXPECT_EQ(profile.emoji_chars, 1);
  EXPECT_EQ(profile.ascii_visible_chars, 4);

  // Old byte-based classification allowed non-Japanese multibyte characters
  // to inflate the Japanese-like byte count. The Unicode-aware profile does
  // not.
  EXPECT_FALSE(analyzer.LooksMostlyJapanese(profile));
}

TEST(ZenzContextScriptAnalyzerTest,
     UnknownScriptCharactersCountAgainstJapaneseDominance) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("日한국어문");

  EXPECT_EQ(profile.japanese_script_chars, 1);
  EXPECT_EQ(profile.other_chars, 4);

  // UNKNOWN_SCRIPT must not become free content merely because one genuine
  // Japanese character exists somewhere in the context.
  EXPECT_FALSE(
      analyzer.LooksMostlyJapanese(profile));
}

TEST(ZenzContextScriptAnalyzerTest,
     OrdinaryJapanesePunctuationRemainsWeakCounterevidence) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("「日。」");

  // 「, 日, 。, 」 are four Unicode codepoints. Only 日 contributes
  // genuine Japanese-script evidence; the three punctuation/bracket
  // characters are UNKNOWN_SCRIPT in Util::GetScriptType().
  EXPECT_EQ(profile.total_chars, 4);
  EXPECT_EQ(profile.japanese_script_chars, 1);
  EXPECT_EQ(profile.other_chars, 3);

  EXPECT_TRUE(
      analyzer.LooksMostlyJapanese(profile));
}

TEST(ZenzContextScriptAnalyzerTest,
     EmojiAreWeakCounterevidenceRatherThanLanguageEvidence) {
  const ZenzContextScriptAnalyzer analyzer;

  {
    const ZenzContextScriptProfile profile =
        analyzer.Analyze("今日は😀😀");

    EXPECT_EQ(profile.japanese_script_chars, 3);
    EXPECT_EQ(profile.emoji_chars, 2);

    EXPECT_TRUE(
        analyzer.LooksMostlyJapanese(profile));
  }

  {
    const ZenzContextScriptProfile profile =
        analyzer.Analyze("日😀😀😀😀");

    EXPECT_EQ(profile.japanese_script_chars, 1);
    EXPECT_EQ(profile.emoji_chars, 4);

    EXPECT_FALSE(
        analyzer.LooksMostlyJapanese(profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     WhitespaceDoesNotOverpowerNormalJapaneseContext) {
  const ZenzContextScriptAnalyzer analyzer;

  const ZenzContextScriptProfile profile =
      analyzer.Analyze("今日は\n明日");

  EXPECT_EQ(profile.japanese_script_chars, 5);

  EXPECT_TRUE(
      analyzer.LooksMostlyJapanese(profile));
}
}  // namespace
}  // namespace session
}  // namespace mozc