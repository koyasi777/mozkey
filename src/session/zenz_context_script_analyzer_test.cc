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

  // Keep the existing persistent feedback vocabulary.
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
TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyPreservesExistingAcceptedContexts) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "今日は",
      "今日はABC",
      "今日は😀😀",
      "「日。」",
      "中文",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksMostlyJapanese(profile));

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyRecoversCurrentMixedScriptFalseNegatives) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "2026年",
      "Windows11を使う",
      "HTTP/2に対応する",
      "macOS15でも使える",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_FALSE(
        analyzer.LooksMostlyJapanese(profile));

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyAcceptsNaturalEmbeddedTechnicalTerms) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "Windows11を使う",
      "GPT-5で生成する",
      "UTF-8に変換する",
      "HTTP/2に対応する",
      "M1で動かす",
      "v3.2へ更新する",
      "C++で書く",
      "macOS15でも使える",
      "これはWindows11",
      "ABC日本語",
      "日本語ABCDE",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyRecognizesJapaneseNumericUnitAttachment) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "2026年",
      "8月",
      "10時",
      "30分",
      "500円",
      "3人",
      "2回",
      "5件",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyRecognizesJapaneseGrammaticalAttachment) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "Windows11を",
      "M1で",
      "UTF-8に",
      "v3.2へ",
      "LongLibraryNameを",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyRecognizesTechnicalNominalSuffix) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "v3.2版",
      "ARM64版",
      "X型",
      "Unix系",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyDoesNotLicenseUnattachedEnglishSpan) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "日本語 ABCDE",
      "This is Englishです",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_FALSE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyDoesNotTreatSingleKanjiAsGenericLicense) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "abcdefghijklmnop日",
      "abcdefghijklmnop本",
      "日abcdefghijklmnop",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_FALSE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyDoesNotRelaxAsciiOnlyOrFullWidthLatin) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "Windows11",
      "2026",
      "ＡＢＣＤＥを",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_FALSE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyPreservesNonJapaneseAndEmojiRejection) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "日한국어문",
      "日😀😀😀😀",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_FALSE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyAllowsNormalPunctuationAroundTechnicalTerm) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "「Windows11を使う」",
      "（GPT-5で生成する）",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}
TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyAcceptsMultiwordTechnicalNames) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "Visual Studioを使う",
      "Visual Studio Codeで書く",
      "OpenAI APIを使う",
      "GitHub Actionsで実行する",
      "Ruby on Railsを使う",
      "Windows Subsystem for Linuxを使う",
      "これはVisual Studio",
      "Microsoft Visual C++を使う",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyAllowsQuotedTechnicalTermsAttachedToJapanese) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "「Windows11」を使う",
      "「Visual Studio」を使う",
      "（OpenAI API）を使う",
      "これは「GPT-5」",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_TRUE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyStillRejectsOrdinaryEnglishSpans) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "This is Englishです",
      "hello worldを",
      "日本語 hello world",
      "「this is text」を使う",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_FALSE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}

TEST(ZenzContextScriptAnalyzerTest,
     CandidatePolicyDoesNotUseWhitespaceAsGenericAttachment) {
  const ZenzContextScriptAnalyzer analyzer;

  const char* const inputs[] = {
      "日本語 Windows11",
      "Windows11 日本語",
      "日本語 ABCDE",
  };

  for (const char* input : inputs) {
    SCOPED_TRACE(input);

    const ZenzContextScriptProfile profile =
        analyzer.Analyze(input);

    EXPECT_FALSE(
        analyzer.LooksUsableAsJapaneseContext(
            input,
            profile));
  }
}
}  // namespace
}  // namespace session
}  // namespace mozc
