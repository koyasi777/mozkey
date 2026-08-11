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
     ContextPolicyAllowsOrdinaryJapaneseTechnicalText) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "2026年",
      "日本語1234",
      "日本語!!!!!!!!",
      "Windows11を使う",
      "GPT-5を使う",
      "UTF-8で保存",
      "HTTP/2に対応",
      "v3.2を使う",
      "M1 Mac",
  };

  for (const char* text : texts) {
    SCOPED_TRACE(text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            text,
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_FALSE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kNone);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     ContextPolicyProtectsAddressAndLocationLikeStructure) {
  const ZenzTextPrivacyAnalyzer analyzer;

  struct TestCase {
    const char* text;
    ZenzTextPrivacySignal signal;
  };

  const TestCase cases[] = {
      {
          "a@b",
          ZenzTextPrivacySignal::kEmailLike,
      },
      {
          "name@host",
          ZenzTextPrivacySignal::kEmailLike,
      },
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
          "mailto:someone",
          ZenzTextPrivacySignal::kUrlOrDomainLike,
      },
      {
          "C:\\Users\\Makoto\\notes.txt",
          ZenzTextPrivacySignal::kPathLike,
      },
      {
          "foo\\bar",
          ZenzTextPrivacySignal::kPathLike,
      },
      {
          "/home/user/notes.txt",
          ZenzTextPrivacySignal::kPathLike,
      },
      {
          "../private/file",
          ZenzTextPrivacySignal::kPathLike,
      },
  };

  for (const TestCase& test : cases) {
    SCOPED_TRACE(test.text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            test.text,
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(result.signal, test.signal);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     ContextPolicyProtectsCredentialVocabulary) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "password=abc",
      "passwd=abc",
      "pwd=abc",
      "passphrase=abc",
      "secret=abc",
      "token=abc",
      "apikey=abc",
      "api_key=abc",
      "credential=abc",
      "privatekey=abc",
      "private_key=abc",
      "authorization=abc",
      "cookie=abc",
      "sessionid=abc",
      "session_id=abc",
      "パスワードを変更",
      "暗証番号を入力",
      "認証コードを入力",
      "認証番号を入力",
      "秘密鍵を保存",
      "秘密キーを保存",
      "トークンを設定",
      "アクセストークンを設定",
      "APIキーを設定",
  };

  for (const char* text : texts) {
    SCOPED_TRACE(text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            text,
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kCredentialWord);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     ContextPolicyProtectsAllKnownSecretPrefixes) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "ghp_abcdefghijklmnopqrstuvwxyz",
      "github_pat_abcdefghijklmnopqrstuvwxyz",
      "glpat-abcdefghijklmnopqrstuvwxyz",
      "sk-abcdefghijklmnopqrstuvwxyz",
      "pk_abcdefghijklmnopqrstuvwxyz",
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
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kSecretPrefix);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     ContextPolicyProtectsOpaqueIdentifiers) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "12345678",
      "192.168.0.1",
      "abc-1234567890",
      "user_12345678",
      "a1b2c3d4e5f6g7h8",
      "deadbeefcafebabe",
  };

  for (const char* text : texts) {
    SCOPED_TRACE(text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            text,
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kTokenLike);
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     ContextPolicyKeepsVersionLikeTokens) {
  const ZenzTextPrivacyAnalyzer analyzer;

  const char* const texts[] = {
      "v0.7.0",
      "v1.0.0-alpha1",
      "v3.2",
      "12345678.1",
      "v12345678.1",
  };

  for (const char* text : texts) {
    SCOPED_TRACE(text);

    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            text,
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_FALSE(result.sensitive());
  }
}

TEST(ZenzTextPrivacyAnalyzerTest,
     ContextPolicyUsesSpecificSignalBeforeOpaqueToken) {
  const ZenzTextPrivacyAnalyzer analyzer;

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "ghp_12345678901234567890",
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kSecretPrefix);
  }

  {
    const ZenzTextPrivacyAnalysis result =
        analyzer.Analyze(
            "password=12345678",
            ZenzTextPrivacyPolicy::kContext);

    EXPECT_TRUE(result.sensitive());
    EXPECT_EQ(
        result.signal,
        ZenzTextPrivacySignal::kCredentialWord);
  }
}
}  // namespace
}  // namespace session
}  // namespace mozc
