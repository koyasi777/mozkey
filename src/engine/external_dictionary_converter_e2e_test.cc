// Copyright 2026 Mozkey contributors.
// End-to-end converter regression tests without installing or launching an IME.
#include <memory>
#include <string>
#include <utility>

#include "base/file_util.h"
#include "base/file/temp_dir.h"
#include "config/config_handler.h"
#include "converter/segments.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/external_dictionary_manager.h"
#include "engine/engine.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

class ExternalDictionaryConverterE2ETest
    : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    original_config_ = config::ConfigHandler::GetCopiedConfig();
    file_ = FileUtil::JoinPath(dir_.path(), "external.tsv");
  }

  void TearDown() override {
    config::ConfigHandler::SetConfig(original_config_);
  }

  void Initialize(const std::string& contents) {
    ASSERT_TRUE(FileUtil::SetContents(file_, contents).ok());
    config::Config config = config::ConfigHandler::GetCopiedConfig();
    config.set_enable_external_dictionary(true);
    config.set_external_dictionary_path(file_);
    config::ConfigHandler::SetConfig(config);

    auto maybe_engine =
        Engine::CreateEngine(std::make_unique<testing::MockDataManager>());
    ASSERT_TRUE(maybe_engine.ok()) << maybe_engine.status();
    engine_ = std::move(*maybe_engine);
    manager_ = engine_->GetModulesForTesting().GetExternalDictionaryManager();
    ASSERT_NE(manager_, nullptr);
    ASSERT_TRUE(manager_->PollOnceForTesting().ok());
  }

  bool HasExternalCandidate(const std::string& key,
                            const std::string& surface) const {
    Segments segments;
    const ConversionRequest request = ConversionRequestBuilder()
        .SetRequestType(ConversionRequest::CONVERSION)
        .SetKey(key)
        .Build();
    if (!engine_->GetConverter()->StartConversion(request, &segments)) {
      ADD_FAILURE() << "StartConversion failed for " << key;
      return false;
    }
    if (segments.conversion_segments_size() != 1) {
      ADD_FAILURE() << "External candidate not injectable: reading=" << key
                    << ", conversion_segments="
                    << segments.conversion_segments_size()
                    << ", segments=" << segments.DebugString();
      return false;
    }
    const Segment& segment = segments.conversion_segment(0);
    for (size_t i = 0; i < segment.candidates_size(); ++i) {
      const auto& c = segment.candidate(i);
      if (c.value == surface && c.description == "外部辞書") {
        return i > 0;  // Native first candidate must stay first.
      }
    }
    ADD_FAILURE() << "External candidate missing: reading=" << key
                  << ", expected=" << surface
                  << ", segments=" << segments.DebugString();
    return false;
  }

  TempDirectory dir_ = testing::MakeTempDirectoryOrDie();
  std::string file_;
  config::Config original_config_;
  std::unique_ptr<Engine> engine_;
  std::shared_ptr<dictionary::ExternalDictionaryManager> manager_;
};

TEST_F(ExternalDictionaryConverterE2ETest, KanaReadingReachesFullConverter) {
  Initialize("ぐうぐる\t外部辞書候補あ\n");
  ASSERT_NE(engine_, nullptr);
  EXPECT_TRUE(HasExternalCandidate("ぐうぐる", "外部辞書候補あ"));
}

TEST_F(ExternalDictionaryConverterE2ETest, NumericCodeReachesFullConverter) {
  Initialize("001234\t外部辞書候補い\n");
  ASSERT_NE(engine_, nullptr);
  EXPECT_TRUE(HasExternalCandidate("001234", "外部辞書候補い"));
}

TEST_F(ExternalDictionaryConverterE2ETest, AtomicReplacementAndDisable) {
  Initialize("ぐうぐる\t外部辞書旧候補\n");
  ASSERT_NE(engine_, nullptr);
  ASSERT_TRUE(HasExternalCandidate("ぐうぐる", "外部辞書旧候補"));

  const std::string replacement = FileUtil::JoinPath(dir_.path(), "next.tsv");
  ASSERT_TRUE(FileUtil::SetContents(replacement,
                                    "ぐうぐる\t外部辞書新候補\n").ok());
  ASSERT_TRUE(FileUtil::AtomicRename(replacement, file_).ok());
  ASSERT_TRUE(manager_->PollOnceForTesting().ok());
  EXPECT_TRUE(HasExternalCandidate("ぐうぐる", "外部辞書新候補"));

  config::Config config = config::ConfigHandler::GetCopiedConfig();
  config.set_enable_external_dictionary(false);
  config::ConfigHandler::SetConfig(config);
  ASSERT_TRUE(engine_->Reload());
  EXPECT_EQ(manager_->GetStatus().state,
            dictionary::ExternalDictionaryManager::State::kDisabled);
  EXPECT_TRUE(manager_->LookupExact("ぐうぐる").empty());
}

}  // namespace
}  // namespace mozc
