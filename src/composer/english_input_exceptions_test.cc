#include "composer/english_input_exceptions.h"

#include <string>

#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc::composer {
namespace {
using Entry = config::Config::EnglishInputException;

TEST(EnglishInputExceptionsTest, NoAutomaticBuiltInWords) {
  config::Config cfg;
  cfg.set_enable_english_input_exceptions(true);
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "github"), nullptr);
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "google"), nullptr);
}

TEST(EnglishInputExceptionsTest, ExactCaseSensitiveAndDisabled) {
  config::Config cfg;
  auto* e = cfg.add_english_input_exceptions();
  e->set_input("github");
  e->set_preferred("GitHub");
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "github"), nullptr);
  cfg.set_enable_english_input_exceptions(true);
  ASSERT_NE(EnglishInputExceptions::Find(cfg, "github"), nullptr);
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "github")->preferred(), "GitHub");
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "GitHub"), nullptr);
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "githubno"), nullptr);
  e->set_enabled(false);
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "github"), nullptr);
}

TEST(EnglishInputExceptionsTest, RulesPersistInConfigProto) {
  config::Config cfg;
  cfg.set_enable_english_input_exceptions(true);
  auto* entry = cfg.add_english_input_exceptions();
  entry->set_input("postgresql");
  entry->set_preferred("PostgreSQL");
  entry->set_behavior(Entry::CANDIDATE_ONLY);
  const std::string serialized = cfg.SerializeAsString();
  config::Config restored;
  ASSERT_TRUE(restored.ParseFromString(serialized));
  const Entry* found = EnglishInputExceptions::Find(restored, "postgresql");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->preferred(), "PostgreSQL");
  EXPECT_EQ(found->behavior(), Entry::CANDIDATE_ONLY);
}

TEST(EnglishInputExceptionsTest, ValidateAndRejectDuplicates) {
  EXPECT_TRUE(EnglishInputExceptions::IsValidInput("go"));
  EXPECT_FALSE(EnglishInputExceptions::IsValidInput("GitHub"));
  EXPECT_FALSE(EnglishInputExceptions::IsValidInput("g+"));
  EXPECT_FALSE(EnglishInputExceptions::IsValidInput("a"));
  EXPECT_TRUE(EnglishInputExceptions::IsValidPreferred("GPT-6"));
  EXPECT_FALSE(EnglishInputExceptions::IsValidPreferred("Line\nBreak"));
  config::Config cfg;
  cfg.set_enable_english_input_exceptions(true);
  auto* a=cfg.add_english_input_exceptions();
  a->set_input("github");
  a->set_preferred("GitHub");
  auto* b=cfg.add_english_input_exceptions();
  b->set_input("github");
  b->set_preferred("NotGitHub");
  EXPECT_EQ(EnglishInputExceptions::Find(cfg, "github"), nullptr);
}

TEST(EnglishInputExceptionsTest, IndexedLookupPreservesFailClosedSemantics) {
  config::Config cfg;
  cfg.set_enable_english_input_exceptions(true);
  auto* first = cfg.add_english_input_exceptions();
  first->set_input("github");
  first->set_preferred("GitHub");
  auto* disabled = cfg.add_english_input_exceptions();
  disabled->set_input("docker");
  disabled->set_preferred("Docker");
  disabled->set_enabled(false);
  auto* invalid = cfg.add_english_input_exceptions();
  invalid->set_input("gitlab");
  invalid->set_preferred("not allowed spaces");
  auto* duplicate = cfg.add_english_input_exceptions();
  duplicate->set_input("github");
  duplicate->set_preferred("Other");
  duplicate->set_enabled(false);
  const EnglishInputExceptions::Index index(cfg);
  EXPECT_EQ(index.Find("github"), nullptr);  // Even disabled duplicates.
  EXPECT_EQ(index.Find("docker"), nullptr);
  EXPECT_EQ(index.Find("gitlab"), nullptr);
  EXPECT_EQ(index.Find("GitHub"), nullptr);
  EXPECT_EQ(index.Find("githubno"), nullptr);
  EXPECT_EQ(index.Find("sushi"), nullptr);  // Never infer English.
  EXPECT_EQ(index.Find("github"), EnglishInputExceptions::Find(cfg, "github"));
}

TEST(EnglishInputExceptionsTest, IndexedLookupHandles4096AndRejects4097) {
  config::Config cfg;
  cfg.set_enable_english_input_exceptions(true);
  std::string first_word;
  std::string last_word;
  for (int i = 0; i < 4096; ++i) {
    // Alphabetic-only unique keys, identical to the supported trigger rules.
    std::string word = "test";
    for (int value = i; value >= 0; value = value / 26 - 1) {
      word.push_back(static_cast<char>('a' + value % 26));
    }
    if (i == 0) first_word = word;
    if (i == 4095) last_word = word;
    auto* entry = cfg.add_english_input_exceptions();
    entry->set_input(word);
    entry->set_preferred("English");
  }
  const EnglishInputExceptions::Index full(cfg);
  ASSERT_NE(full.Find(first_word), nullptr);
  ASSERT_NE(full.Find(last_word), nullptr);
  EXPECT_EQ(full.Find("noentry"), nullptr);
  auto* extra = cfg.add_english_input_exceptions();
  extra->set_input("extra");
  extra->set_preferred("Extra");
  const EnglishInputExceptions::Index oversized(cfg);
  EXPECT_EQ(oversized.Find(first_word), nullptr);
  EXPECT_EQ(oversized.Find(last_word), nullptr);
}
}  // namespace
}  // namespace mozc::composer
