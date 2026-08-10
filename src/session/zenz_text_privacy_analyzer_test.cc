#include "session/zenz_text_privacy_analyzer.h"

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzTextPrivacyAnalyzerTest,
     LivePolicyPreservesSensitiveStructureDetection) {
  const ZenzTextPrivacyAnalyzer analyzer;

  struct TestCase {
    const char* text;
    ZenzTextPrivacySignal signal;
  };

  const TestCase cases[] = {
      {
          "user@example.com",
          ZenzTextPrivacySignal::kEmailLike,
      },
      {
          "https://example.com",
          ZenzTextPrivacySignal::kUrlOrDomainLike,
      },
      {
          "example.com",
          ZenzTextPrivacySignal::kUrlOrDomainLike,
      },
      {
          "C:\\Users\\Makoto\\notes.txt",
          ZenzTextPrivacySignal::kPathLike,
      },
      {
          "ghp_abcdefghijklmnopqrstuvwxyz",
          ZenzTextPrivacySignal::kSecretPrefix,
      },
      {
          "12345678",
          ZenzTextPrivacySignal::kTokenLike,
      },
      {
          "a1b2c3d4e5f6g7h8",
          ZenzTextPrivacySignal::kTokenLike,
      },
      {
          "password=example",
          ZenzTextPrivacySignal::kCredentialWord,
      },
      {
          "パスワードは秘密",
          ZenzTextPrivacySignal::kCredentialWord,
      },
  };

  for (const TestCase& test : cases) {
    SCOPED_TRACE(test.text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            test.text,
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(result.signal, test.signal);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     LivePolicyPreservesOrdinaryTechnicalText) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "Windows11",
      "2026",
      "GPT-5",
      "v0.7.0",
      "v1.0.0-alpha1",
      "HTTP/2",
      "M1",
      "UTF-8",
  };

  for (const char* text : texts) {
    SCOPED_TRACE(text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            text,
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_FALSE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kNone);
    EXPECT_STREQ(
        result.reason(),
        "allow");
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     LivePolicyPreservesKnownSecretPrefixes) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "ghp_abcdefghijklmnopqrstuvwxyz",
      "github_pat_abcdefghijklmnopqrstuvwxyz",
      "glpat-abcdefghijklmnopqrstuvwxyz",
      "sk-abcdefghijklmnopqrstuvwxyz",
      "xoxb-abcdefghijklmnopqrstuvwxyz",
      "xoxp-abcdefghijklmnopqrstuvwxyz",
      "ya29.abcdefghijklmnopqrstuvwxyz",
      "AKIAIOSFODNN7EXAMPLE",
      "Bearer abcdefghijklmnopqrstuvwxyz",
  };

  for (const char* text : texts) {
    SCOPED_TRACE(text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            text,
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_TRUE(result.sensitive());
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     LivePolicyPreservesOpaqueTokenThresholds) {
  const ZenzTextPrivacyAnalyzer analyzer;

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "1234567",
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_FALSE(result.sensitive());
  }

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "12345678",
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kTokenLike);
  }

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "abc-12345678",
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kTokenLike);
  }

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "a1b2c3d4e5f6g7h8",
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kTokenLike);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     VersionLikeTokenRemainsNonSensitive) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const versions[] = {
      "v0.7.0",
      "v1.0.0-alpha1",
      "12345678.1",
      "v12345678.1",
  };

  for (const char* version : versions) {
    SCOPED_TRACE(version);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            version,
            ZenzTextPrivacyPolicy::kLiveText);

    EXPECT_FALSE(result.sensitive());
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     Ipv4LikeTokenRemainsSensitive) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const ZenzTextPrivacyAnalysis result =
      analyzer.Analyze(
          "192.168.0.1",
          ZenzTextPrivacyPolicy::kLiveText);

  EXPECT_TRUE(result.sensitive());
  EXPECT_EQ(
      result.signal,
      ZenzTextPrivacySignal::kTokenLike);
}

TEST(ZenzTextPrivacyAnalyzerTest,
     LegacyContextPolicyPreservesFourDigitRule) {
  const ZenzTextPrivacyAnalyzer analyzer;

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "日本語123",
            ZenzTextPrivacyPolicy::kLegacyContext);

    EXPECT_FALSE(result.sensitive());
  }

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "日本語1234",
            ZenzTextPrivacyPolicy::kLegacyContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::
            kLegacyLongDigitRun);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     LegacyContextPolicyPreservesEightVisibleAsciiRule) {
  const ZenzTextPrivacyAnalyzer analyzer;

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "日本語!!!!!!!",
            ZenzTextPrivacyPolicy::kLegacyContext);

    EXPECT_FALSE(result.sensitive());
  }

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "日本語!!!!!!!!",
            ZenzTextPrivacyPolicy::kLegacyContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::
            kLegacyLongVisibleAscii);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     LegacyContextPolicyPreservesSensitivePatterns) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "https://example.com",
      "www.example.com",
      "mailto:user@example.com",
      "foo@example.com",
      "C:\\Users\\Makoto\\notes.txt",
      "/home/user/notes.txt",
      "password=abc",
      "passwd=abc",
      "pwd=abc",
      "token=abc",
      "secret=abc",
      "apikey=abc",
      "api_key=abc",
      "authorization=abc",
      "bearer abc",
      "cookie=abc",
      "sessionid=abc",
      "session_id=abc",
      "sk-abc",
      "pk_abc",
      "ghp_abc",
      "xoxb-abc",
  };

  for (const char* text : texts) {
    SCOPED_TRACE(text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            text,
            ZenzTextPrivacyPolicy::kLegacyContext);

    EXPECT_TRUE(result.sensitive());
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     PoliciesPreserveHistoricalEmailDifference) {
  const ZenzTextPrivacyAnalyzer analyzer;

  // The live policy treats any '@' as address/handle-like content.
  // The legacy context policy requires both '@' and '.' for its email rule.
  // Keep this example below the legacy eight-visible-ASCII threshold so the
  // policy difference itself is observable.
  const ZenzTextPrivacyAnalysis short_live =
      analyzer.Analyze(
          "a@b",
          ZenzTextPrivacyPolicy::kLiveText);

  const ZenzTextPrivacyAnalysis short_context =
      analyzer.Analyze(
          "a@b",
          ZenzTextPrivacyPolicy::kLegacyContext);

  EXPECT_TRUE(short_live.sensitive());
  EXPECT_EQ(
      short_live.signal,
      ZenzTextPrivacySignal::kEmailLike);

  EXPECT_FALSE(short_context.sensitive());
  EXPECT_EQ(
      short_context.signal,
      ZenzTextPrivacySignal::kNone);

  // A longer '@'-containing value still does not match the legacy email
  // heuristic without '.', but the separate historical eight-visible-ASCII
  // fallback makes the context sensitive for a different reason.
  const ZenzTextPrivacyAnalysis long_live =
      analyzer.Analyze(
          "name@host",
          ZenzTextPrivacyPolicy::kLiveText);

  const ZenzTextPrivacyAnalysis long_context =
      analyzer.Analyze(
          "name@host",
          ZenzTextPrivacyPolicy::kLegacyContext);

  EXPECT_TRUE(long_live.sensitive());
  EXPECT_EQ(
      long_live.signal,
      ZenzTextPrivacySignal::kEmailLike);

  EXPECT_TRUE(long_context.sensitive());
  EXPECT_EQ(
      long_context.signal,
      ZenzTextPrivacySignal::kLegacyLongVisibleAscii);
}

TEST(ZenzTextPrivacyAnalyzerTest,
     PoliciesPreserveHistoricalPathDifference) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const ZenzTextPrivacyAnalysis live =
      analyzer.Analyze(
          "foo\\bar",
          ZenzTextPrivacyPolicy::kLiveText);

  const ZenzTextPrivacyAnalysis context =
      analyzer.Analyze(
          "foo\\bar",
          ZenzTextPrivacyPolicy::kLegacyContext);

  EXPECT_TRUE(live.sensitive());
  EXPECT_EQ(
      live.signal,
      ZenzTextPrivacySignal::kPathLike);

  EXPECT_FALSE(context.sensitive());
}

}  // namespace
}  // namespace session
}  // namespace mozc