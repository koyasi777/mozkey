// Copyright 2026 Mozkey contributors.
// SessionHandler-level external-dictionary tests; no installed IME is touched.
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "base/file_util.h"
#include "base/file/temp_dir.h"
#include "config/config_handler.h"
#include "data_manager/data_manager.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/external_dictionary_manager.h"
#include "engine/engine.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/session_handler.h"
#include "session/session_handler_test_util.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

class ExternalDictionarySessionHandlerE2ETest
    : public session::testing::SessionHandlerTestBase {
 protected:
  void SetUp() override {
    SessionHandlerTestBase::SetUp();
    path_ = FileUtil::JoinPath(dir_.path(), "session_external.tsv");
  }

  void TearDown() override {
    // Release the manager's worker before clearing its temporary data.
    handler_.reset();
    manager_.reset();
    SessionHandlerTestBase::TearDown();
  }

  void Initialize(bool use_oss_data) {
    std::unique_ptr<Engine> engine;
    if (use_oss_data) {
      const std::string data_path = testing::GetSourcePath(
          {"data_manager", "oss", "mozc.data"});
      auto data = DataManager::CreateFromFile(data_path, "\xEFMOZC\r\n");
      ASSERT_TRUE(data.ok()) << data.status();
      auto created = Engine::CreateEngine(std::move(*data));
      ASSERT_TRUE(created.ok()) << created.status();
      engine = std::move(*created);
    } else {
      auto created = Engine::CreateEngine(
          std::make_unique<testing::MockDataManager>());
      ASSERT_TRUE(created.ok()) << created.status();
      engine = std::move(*created);
    }

    manager_ = engine->GetModulesForTesting().GetExternalDictionaryManager();
    ASSERT_NE(manager_, nullptr);
    handler_ = std::make_unique<SessionHandler>(std::move(engine));
    ASSERT_TRUE(handler_->IsAvailable());

    commands::Command create;
    create.mutable_input()->set_type(commands::Input::CREATE_SESSION);
    create.mutable_input()->mutable_capability()->set_text_deletion(
        commands::Capability::DELETE_PRECEDING_TEXT);
    ASSERT_TRUE(handler_->EvalCommand(&create));
    ASSERT_EQ(create.output().error_code(),
              commands::Output::SESSION_SUCCESS);
    session_id_ = create.output().id();
    ASSERT_NE(session_id_, 0u);
  }

  bool SendConfig(bool enabled, const std::string& path) {
    config::Config config = config::ConfigHandler::GetCopiedConfig();
    config.set_enable_external_dictionary(enabled);
    config.set_external_dictionary_path(path);
    config.set_use_live_conversion(false);
    config.set_use_zenz_live_correction(false);
    config.set_show_candidate_window_on_initial_conversion(true);

    commands::Command set;
    set.mutable_input()->set_type(commands::Input::SET_CONFIG);
    *set.mutable_input()->mutable_config() = config;
    if (!handler_->EvalCommand(&set) ||
        set.output().error_code() != commands::Output::SESSION_SUCCESS) {
      ADD_FAILURE() << "SET_CONFIG failed: " << set.DebugString();
      return false;
    }
    commands::Command get;
    get.mutable_input()->set_type(commands::Input::GET_CONFIG);
    if (!handler_->EvalCommand(&get) || !get.output().has_config()) {
      ADD_FAILURE() << "GET_CONFIG failed: " << get.DebugString();
      return false;
    }
    const auto& actual = get.output().config();
    if (actual.enable_external_dictionary() != enabled ||
        actual.external_dictionary_path() != path) {
      ADD_FAILURE() << "SET_CONFIG round-trip mismatch: "
                    << actual.ShortDebugString();
      return false;
    }
    return true;
  }

  bool SendInput(commands::Input::CommandType type,
                 commands::SessionCommand::CommandType command_type) {
    commands::Command command;
    command.mutable_input()->set_id(session_id_);
    command.mutable_input()->set_type(type);
    command.mutable_input()->mutable_command()->set_type(command_type);
    return handler_->EvalCommand(&command) &&
           command.output().error_code() == commands::Output::SESSION_SUCCESS;
  }

  bool ConvertReading(const std::string& reading, commands::Output* output) {
    // Each request starts from an empty composition in the SAME session.
    if (!SendInput(commands::Input::SEND_COMMAND,
                   commands::SessionCommand::RESET_CONTEXT)) {
      ADD_FAILURE() << "RESET_CONTEXT failed";
      return false;
    }
    commands::Command on;
    on.mutable_input()->set_id(session_id_);
    on.mutable_input()->set_type(commands::Input::SEND_KEY);
    on.mutable_input()->mutable_key()->set_special_key(commands::KeyEvent::ON);
    if (!handler_->EvalCommand(&on)) {
      ADD_FAILURE() << "IME ON failed";
      return false;
    }

    commands::Command update;
    update.mutable_input()->set_id(session_id_);
    update.mutable_input()->set_type(commands::Input::SEND_COMMAND);
    auto* composition = update.mutable_input()->mutable_command();
    composition->set_type(commands::SessionCommand::UPDATE_COMPOSITION);
    auto* event = composition->add_composition_events();
    event->set_composition_string(reading);
    event->set_probability(1.0);
    if (!handler_->EvalCommand(&update) || !update.output().has_preedit()) {
      ADD_FAILURE() << "UPDATE_COMPOSITION failed: " << update.DebugString();
      return false;
    }

    commands::Command convert;
    convert.mutable_input()->set_id(session_id_);
    convert.mutable_input()->set_type(commands::Input::SEND_KEY);
    convert.mutable_input()->mutable_key()->set_special_key(
        commands::KeyEvent::SPACE);
    if (!handler_->EvalCommand(&convert)) {
      ADD_FAILURE() << "SPACE conversion failed";
      return false;
    }
    *output = convert.output();
    if (!output->has_candidate_window() &&
        !output->has_all_candidate_words()) {
      ADD_FAILURE() << "No conversion candidates returned: "
                    << output->ShortDebugString();
      return false;
    }
    return true;
  }

  bool ContainsSurface(const commands::Output& output,
                       const std::string& surface) {
    if (output.has_all_candidate_words()) {
      for (const auto& candidate : output.all_candidate_words().candidates()) {
        if (candidate.value() == surface) {
          EXPECT_GT(candidate.index(), 0u)
              << "External entry must not displace the native top candidate";
          return true;
        }
      }
    }
    if (output.has_candidate_window()) {
      for (const auto& candidate : output.candidate_window().candidate()) {
        if (candidate.value() == surface) {
          EXPECT_GT(candidate.index(), 0u)
              << "External entry must not displace the native top candidate";
          return true;
        }
      }
    }
    return false;
  }

  TempDirectory dir_ = testing::MakeTempDirectoryOrDie();
  std::string path_;
  uint64_t session_id_ = 0;
  std::shared_ptr<dictionary::ExternalDictionaryManager> manager_;
  std::unique_ptr<SessionHandler> handler_;
};

TEST_F(ExternalDictionarySessionHandlerE2ETest,
       ExistingSessionConfigEnableReplaceAndDisable) {
  const std::string original = "外部辞書セッション候補甲";
  const std::string replacement = "外部辞書セッション候補乙";
  ASSERT_TRUE(FileUtil::SetContents(path_,
                                    "ぐうぐる\t" + original + "\n").ok());
  Initialize(false);
  ASSERT_NE(handler_, nullptr);

  // SET_CONFIG must trigger SessionHandler::MaybeUpdateConfig -> Engine::Reload.
  ASSERT_TRUE(SendConfig(true, path_));
  ASSERT_TRUE(manager_->PollOnceForTesting().ok());
  commands::Output out;
  ASSERT_TRUE(ConvertReading("ぐうぐる", &out));
  EXPECT_TRUE(ContainsSurface(out, original)) << out.ShortDebugString();

  // Atomic file replacement is detected without changing the config.
  const std::string next = FileUtil::JoinPath(dir_.path(), "next.tsv");
  ASSERT_TRUE(FileUtil::SetContents(next,
                                    "ぐうぐる\t" + replacement + "\n").ok());
  ASSERT_TRUE(FileUtil::AtomicRename(next, path_).ok());
  ASSERT_TRUE(manager_->PollOnceForTesting().ok());
  ASSERT_TRUE(ConvertReading("ぐうぐる", &out));
  EXPECT_FALSE(ContainsSurface(out, original));
  EXPECT_TRUE(ContainsSurface(out, replacement)) << out.ShortDebugString();

  ASSERT_TRUE(SendConfig(false, path_));
  EXPECT_EQ(manager_->GetStatus().state,
            dictionary::ExternalDictionaryManager::State::kDisabled);
  ASSERT_TRUE(ConvertReading("ぐうぐる", &out));
  EXPECT_FALSE(ContainsSurface(out, replacement)) << out.ShortDebugString();
}

// With the actual OSS dictionary, a short standalone reading can stay within
// a single conversion segment. Exercise the full SessionHandler/Engine/rewriter
// path instead of assuming the mock-data segment boundaries generalize.
TEST_F(ExternalDictionarySessionHandlerE2ETest,
       OssDictionarySingleSegmentSetConfigReachesCandidateOutput) {
  const std::string surface = "外部辞書実辞書候補";
  ASSERT_TRUE(FileUtil::SetContents(path_,
                                    "ぐう\t" + surface + "\n").ok());
  Initialize(true);
  ASSERT_NE(handler_, nullptr);
  ASSERT_TRUE(SendConfig(true, path_));
  ASSERT_TRUE(manager_->PollOnceForTesting().ok());

  commands::Output out;
  ASSERT_TRUE(ConvertReading("ぐう", &out));
  ASSERT_EQ(out.preedit().segment_size(), 1)
      << "OSS dictionary no longer produces a single segment: "
      << out.ShortDebugString();
  EXPECT_TRUE(ContainsSurface(out, surface)) << out.ShortDebugString();
}

// An exact external reading must be selectable even when the native OSS
// converter splits the reading into multiple segments. Native segmentation and
// the first native candidate remain unchanged until explicit selection.
TEST_F(ExternalDictionarySessionHandlerE2ETest,
       OssDictionaryMultiSegmentReadingSelectAndCommit) {
  const std::string surface = "外部辞書複数文節候補";
  ASSERT_TRUE(FileUtil::SetContents(path_,
                                    "ぐうぐる\t" + surface + "\n").ok());
  Initialize(true);
  ASSERT_NE(handler_, nullptr);
  ASSERT_TRUE(SendConfig(true, path_));
  ASSERT_TRUE(manager_->PollOnceForTesting().ok());

  commands::Output out;
  ASSERT_TRUE(ConvertReading("ぐうぐる", &out));
  ASSERT_EQ(out.preedit().segment_size(), 2)
      << "Unexpected native OSS segmentation: " << out.ShortDebugString();
  ASSERT_TRUE(out.has_candidate_window());
  ASSERT_GT(out.candidate_window().candidate_size(), 1);
  EXPECT_NE(out.candidate_window().candidate(0).value(), surface);
  ASSERT_TRUE(ContainsSurface(out, surface)) << out.ShortDebugString();

  int candidate_id = -1;
  for (const auto& candidate : out.candidate_window().candidate()) {
    if (candidate.value() == surface) {
      candidate_id = candidate.id();
      break;
    }
  }
  ASSERT_GE(candidate_id, 1) << out.ShortDebugString();

  // Use exactly the same candidate-submit command as an actual candidate click.
  commands::Command submit;
  submit.mutable_input()->set_id(session_id_);
  submit.mutable_input()->set_type(commands::Input::SEND_COMMAND);
  submit.mutable_input()->mutable_command()->set_type(
      commands::SessionCommand::SUBMIT_CANDIDATE);
  submit.mutable_input()->mutable_command()->set_id(candidate_id);
  ASSERT_TRUE(handler_->EvalCommand(&submit)) << submit.ShortDebugString();
  ASSERT_TRUE(submit.output().has_result()) << submit.ShortDebugString();
  EXPECT_EQ(submit.output().result().key(), "ぐうぐる");
  EXPECT_EQ(submit.output().result().value(), surface);
}

TEST_F(ExternalDictionarySessionHandlerE2ETest,
       OssDictionaryRejectsPrefixOnlyExternalReading) {
  const std::string surface = "外部辞書部分一致禁止候補";
  ASSERT_TRUE(FileUtil::SetContents(path_,
                                    "ぐうぐる\t" + surface + "\n").ok());
  Initialize(true);
  ASSERT_NE(handler_, nullptr);
  ASSERT_TRUE(SendConfig(true, path_));
  ASSERT_TRUE(manager_->PollOnceForTesting().ok());

  commands::Output out;
  ASSERT_TRUE(ConvertReading("ぐうぐるに", &out));
  EXPECT_FALSE(ContainsSurface(out, surface)) << out.ShortDebugString();
}

}  // namespace
}  // namespace mozc
