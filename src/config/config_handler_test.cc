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

#include "config/config_handler.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "base/thread.h"
#include "protocol/config.pb.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace config {
namespace {

class ConfigHandlerTest : public testing::TestWithTempUserProfile {
 protected:
  ConfigHandlerTest()
      : default_config_filename_(ConfigHandler::GetConfigFileNameForTesting()) {
  }
  ~ConfigHandlerTest() override {
    ConfigHandler::SetConfigFileNameForTesting(default_config_filename_);
  }

 private:
  std::string default_config_filename_;
};

constexpr uint32_t kExpectedMozkeyDirectCommitKey =
    Config::DIRECT_COMMIT_KUTEN |
    Config::DIRECT_COMMIT_TOUTEN |
    Config::DIRECT_COMMIT_QUESTION_MARK |
    Config::DIRECT_COMMIT_EXCLAMATION_MARK |
    Config::DIRECT_COMMIT_OPEN_BRACKET |
    Config::DIRECT_COMMIT_CLOSE_BRACKET;

void SetMozkeyProductDefaultsForTesting(Config* config) {
  config->set_use_live_conversion(true);
  config->set_show_candidate_window_on_initial_conversion(true);
  config->set_use_direct_commit(true);
  config->set_direct_commit_key(kExpectedMozkeyDirectCommitKey);
  config->set_use_zenz_live_correction(true);
  config->set_use_zenz_feedback_learning(true);
  config->set_use_zenz_feedback_min_key_length(true);
  config->set_zenz_feedback_min_key_length(7);
  config->set_use_zenz_auto_block_rejected_correction(true);
  config->set_use_zenz_live_correction_right_context(true);
  config->set_use_realtime_conversion(false);
#ifdef _WIN32
  config->set_dim_pending_roman_input(false);
  config->set_pending_roman_dimness_percent(85);
  config->set_use_custom_preedit_underline_color(true);
  config->set_preedit_underline_color(0x30dcc8);
  config->set_use_custom_preedit_target_underline_color(true);
  config->set_preedit_target_underline_color(0xc1a5ab);
#endif  // _WIN32
}

void ExpectMozkeyProductDefaults(const Config& config) {
  EXPECT_TRUE(config.use_live_conversion());
  EXPECT_EQ(config.live_conversion_delay_msec(), 228);
  EXPECT_EQ(config.live_conversion_min_key_length(), 2);
  EXPECT_TRUE(config.show_candidate_window_on_initial_conversion());

  EXPECT_TRUE(config.use_direct_commit());
  EXPECT_EQ(config.direct_commit_key(), kExpectedMozkeyDirectCommitKey);

  EXPECT_TRUE(config.use_zenz_live_correction());
  EXPECT_FALSE(config.defer_live_conversion_display_until_zenz_result());
  EXPECT_EQ(config.zenz_live_correction_delay_msec(), 1000);
  EXPECT_EQ(config.zenz_deferred_presentation_timeout_msec(), 250);
  EXPECT_EQ(config.zenz_live_correction_timeout_msec(), 180);
  EXPECT_EQ(config.zenz_live_correction_min_key_length(), 2);
  EXPECT_EQ(config.zenz_live_correction_left_context_length(), 24);
  EXPECT_TRUE(config.use_zenz_synthetic_candidate());
  EXPECT_TRUE(config.use_zenz_feedback_learning());
  EXPECT_TRUE(config.use_zenz_feedback_min_key_length());
  EXPECT_EQ(config.zenz_feedback_min_key_length(), 7);
  EXPECT_TRUE(config.use_zenz_auto_block_rejected_correction());
  EXPECT_EQ(config.zenz_auto_block_reject_threshold(), 1);
  EXPECT_EQ(config.zenz_auto_block_minimum_reject_percentage(), 50);
  EXPECT_TRUE(config.use_zenz_live_correction_right_context());
  EXPECT_EQ(config.zenz_live_correction_right_context_length(), 6);

  EXPECT_EQ(config.history_learning_level(), Config::DEFAULT_HISTORY);
  EXPECT_TRUE(config.use_history_suggest());
  EXPECT_TRUE(config.use_dictionary_suggest());
  EXPECT_FALSE(config.use_realtime_conversion());
#ifdef _WIN32
  EXPECT_FALSE(config.dim_pending_roman_input());
  EXPECT_EQ(config.pending_roman_dimness_percent(), 85);
  EXPECT_TRUE(config.use_custom_preedit_underline_color());
  EXPECT_EQ(config.preedit_underline_color(), 0x30dcc8);
  EXPECT_TRUE(config.use_custom_preedit_target_underline_color());
  EXPECT_EQ(config.preedit_target_underline_color(), 0xc1a5ab);
  EXPECT_TRUE(config.use_custom_zenz_live_correction_underline_color());
  EXPECT_EQ(config.zenz_live_correction_underline_color(), 0xc4dc6c);
#endif  // _WIN32
  EXPECT_EQ(config.suggestions_size(), 3);
}

TEST_F(ConfigHandlerTest, SetConfig) {
  Config input;
  Config output;

  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "mozc_config_test_tmp");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);
  EXPECT_EQ(ConfigHandler::GetConfigFileNameForTesting(), config_file);
  ConfigHandler::Reload();

  ConfigHandler::GetDefaultConfig(&input);
  input.set_incognito_mode(true);
#ifndef NDEBUG
  input.set_verbose_level(2);
#endif  // NDEBUG
  Config expected = input;
  SetMozkeyProductDefaultsForTesting(&expected);

  ConfigHandler::SetConfig(input);
  output = ConfigHandler::GetCopiedConfig();
  config::Config output2 = ConfigHandler::GetCopiedConfig();
  expected.clear_general_config();
  output.clear_general_config();
  output2.clear_general_config();
  EXPECT_EQ(absl::StrCat(output), absl::StrCat(expected));
  EXPECT_EQ(absl::StrCat(output2), absl::StrCat(expected));

  ConfigHandler::GetDefaultConfig(&input);
  input.set_incognito_mode(false);
#ifndef NDEBUG
  input.set_verbose_level(0);
#endif  // NDEBUG
  expected = input;
  SetMozkeyProductDefaultsForTesting(&expected);

  ConfigHandler::SetConfig(input);
  output = ConfigHandler::GetCopiedConfig();
  output2 = ConfigHandler::GetCopiedConfig();

  expected.clear_general_config();
  output.clear_general_config();
  output2.clear_general_config();
  EXPECT_EQ(absl::StrCat(output), absl::StrCat(expected));
  EXPECT_EQ(absl::StrCat(output2), absl::StrCat(expected));
}

TEST_F(ConfigHandlerTest, CustomKeymapAddsCandidateHideBindingWhenFree) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "custom_keymap_migration.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config input;
  input.set_session_keymap(Config::CUSTOM);
  input.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Prediction\tCtrl Delete\tDeleteSelectedCandidate\n");

  ConfigHandler::SetConfig(input);
  const Config output = ConfigHandler::GetCopiedConfig();
  constexpr absl::string_view kMigrationRow =
      "Conversion\tCtrl Delete\tDeleteSelectedCandidate";
  const size_t first = output.custom_keymap_table().find(kMigrationRow);
  ASSERT_NE(first, std::string::npos);
  EXPECT_EQ(output.custom_keymap_table().find(kMigrationRow, first + 1),
            std::string::npos);

  // Applying the already-migrated config again must not duplicate the row.
  ConfigHandler::SetConfig(output);
  const Config output2 = ConfigHandler::GetCopiedConfig();
  const size_t first2 = output2.custom_keymap_table().find(kMigrationRow);
  ASSERT_NE(first2, std::string::npos);
  EXPECT_EQ(output2.custom_keymap_table().find(kMigrationRow, first2 + 1),
            std::string::npos);

  std::ifstream ifs(config_file, std::ios::binary);
  ASSERT_TRUE(ifs);
  const std::string stored((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
  EXPECT_NE(stored.find("Conversion\tCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);
}

TEST_F(ConfigHandlerTest, CustomKeymapMigrationPersistsOnReload) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "custom_keymap_reload_migration.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config legacy;
  legacy.set_session_keymap(Config::CUSTOM);
  legacy.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Prediction\tCtrl Delete\tDeleteSelectedCandidate\n");

  {
    std::ofstream ofs(config_file, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(ofs);
    const std::string serialized = legacy.SerializeAsString();
    ofs.write(serialized.data(), serialized.size());
    ASSERT_TRUE(ofs);
  }

  ConfigHandler::Reload();
  const Config output = ConfigHandler::GetCopiedConfig();
  EXPECT_NE(output.custom_keymap_table().find(
                "Conversion\tCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);

  std::ifstream ifs(config_file, std::ios::binary);
  ASSERT_TRUE(ifs);
  Config stored;
  ASSERT_TRUE(stored.ParseFromIstream(&ifs));
  EXPECT_NE(stored.custom_keymap_table().find(
                "Conversion\tCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);
}

TEST_F(ConfigHandlerTest,
       CustomKeymapMigrationPreservesExistingAlternateDeleteBinding) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "custom_keymap_alternate.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config input;
  input.set_session_keymap(Config::CUSTOM);
  input.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tAlt Delete\tDeleteSelectedCandidate\n");

  ConfigHandler::SetConfig(input);
  const Config output = ConfigHandler::GetCopiedConfig();
  EXPECT_NE(output.custom_keymap_table().find(
                "Conversion\tAlt Delete\tDeleteSelectedCandidate"),
            std::string::npos);
  EXPECT_EQ(output.custom_keymap_table().find(
                "Conversion\tCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);
}

TEST_F(
    ConfigHandlerTest,
    CustomKeymapMigrationPreservesAlternateDeleteBindingWithLaterRows) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(),
                         "custom_keymap_alternate_with_later_rows.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config input;
  input.set_session_keymap(Config::CUSTOM);
  input.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tRightCtrl Delete\tDeleteSelectedCandidate\n"
      "Conversion\tCtrl y\tBackspace\n"
      "Conversion\tON\tIMEOn\n");

  ConfigHandler::SetConfig(input);
  const Config output = ConfigHandler::GetCopiedConfig();
  EXPECT_NE(output.custom_keymap_table().find(
                "Conversion\tRightCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);
  EXPECT_EQ(output.custom_keymap_table().find(
                "Conversion\tCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);
}

TEST_F(ConfigHandlerTest,
       CustomKeymapMigrationFindsDeleteCommandAnywhereInSequence) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(),
                         "custom_keymap_alternate_command_sequence.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config input;
  input.set_session_keymap(Config::CUSTOM);
  input.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tAlt Delete\tCancel | DeleteSelectedCandidate\n"
      "Conversion\tCtrl y\tBackspace\n");

  ConfigHandler::SetConfig(input);
  const Config output = ConfigHandler::GetCopiedConfig();
  EXPECT_EQ(output.custom_keymap_table().find(
                "Conversion\tCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);
}

TEST_F(ConfigHandlerTest, CustomKeymapMigrationPreservesOccupiedCtrlDelete) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "custom_keymap_ctrl_delete_busy.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config input;
  input.set_session_keymap(Config::CUSTOM);
  input.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tCtrl Delete\tCancel\n");

  ConfigHandler::SetConfig(input);
  const Config output = ConfigHandler::GetCopiedConfig();
  EXPECT_NE(output.custom_keymap_table().find(
                "Conversion\tCtrl Delete\tCancel"),
            std::string::npos);
  EXPECT_EQ(output.custom_keymap_table().find(
                "Conversion\tCtrl Delete\tDeleteSelectedCandidate"),
            std::string::npos);
}

TEST_F(ConfigHandlerTest, CustomKeymapMigratesZenzSpaceExactlyOnce) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "zenz_space_migration.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config config;
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tSpace\tConvertNext\n"
      "Conversion\tEnter\tCommit\n");
  ConfigHandler::SetConfig(config);

  const std::string migrated =
      ConfigHandler::GetCopiedConfig().custom_keymap_table();
  constexpr absl::string_view row =
      "ZenzConversion\tSpace\tRevertZenzToMozc";
  const size_t at = migrated.find(row);
  ASSERT_NE(at, std::string::npos);
  EXPECT_EQ(migrated.find(row, at + 1), std::string::npos);
  EXPECT_NE(migrated.find("Conversion\tSpace\tConvertNext"),
            std::string::npos);
  EXPECT_NE(migrated.find("Conversion\tEnter\tCommit"),
            std::string::npos);

  ConfigHandler::SetConfig(ConfigHandler::GetCopiedConfig());
  EXPECT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(), migrated);

  // The migrated row must survive storage and a subsequent reload.
  ConfigHandler::Reload();
  EXPECT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(), migrated);
  std::ifstream ifs(config_file, std::ios::binary);
  ASSERT_TRUE(ifs);
  Config stored;
  ASSERT_TRUE(stored.ParseFromIstream(&ifs));
  EXPECT_EQ(stored.custom_keymap_table(), migrated);
}

TEST_F(ConfigHandlerTest, CustomKeymapZenzMigrationOnReloadPersists) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "zenz_space_reload.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config legacy;
  legacy.set_session_keymap(Config::CUSTOM);
  legacy.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tSpace\tConvertNext\n");
  {
    std::ofstream ofs(config_file, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(ofs);
    const std::string bytes = legacy.SerializeAsString();
    ofs.write(bytes.data(), bytes.size());
    ASSERT_TRUE(ofs);
  }
  ConfigHandler::Reload();
  const auto migrated = ConfigHandler::GetCopiedConfig().custom_keymap_table();
  EXPECT_NE(migrated.find("ZenzConversion\tSpace\tRevertZenzToMozc"),
            std::string::npos);
  ConfigHandler::Reload();
  EXPECT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(), migrated);
  std::ifstream ifs(config_file, std::ios::binary);
  ASSERT_TRUE(ifs);
  Config stored;
  ASSERT_TRUE(stored.ParseFromIstream(&ifs));
  EXPECT_EQ(stored.custom_keymap_table(), migrated);
}

TEST_F(ConfigHandlerTest, CustomKeymapZenzMigrationPreservesUserSpaceRules) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "zenz_space_custom_rules.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  constexpr absl::string_view kZenzRevert =
      "ZenzConversion\tSpace\tRevertZenzToMozc";
  const std::vector<std::string> custom_tables = {
      // User explicitly selected a different Zenz action.
      "status\tkey\tcommand\n"
      "Conversion\tSpace\tConvertNext\n"
      "ZenzConversion\tSpace\tCommit\n",
      // Non-standard base action and multi-step command: leave untouched.
      "status\tkey\tcommand\n"
      "Conversion\tSpace\tConvertPrev\n",
      "status\tkey\tcommand\n"
      "Conversion\tSpace\tConvertNext|ConvertNext\n",
      // Missing base action and multiple base mappings are ambiguous.
      "status\tkey\tcommand\n"
      "Conversion\tEnter\tCommit\n",
      "status\tkey\tcommand\n"
      "Conversion\tSpace\tConvertNext\n"
      "Conversion\tSpace\tCommit\n",
  };
  for (const std::string& table : custom_tables) {
    Config config;
    config.set_session_keymap(Config::CUSTOM);
    config.set_custom_keymap_table(table);
    ConfigHandler::SetConfig(config);
    const std::string saved =
        ConfigHandler::GetCopiedConfig().custom_keymap_table();
    EXPECT_EQ(saved.find(kZenzRevert), std::string::npos);
    EXPECT_NE(saved.find(table.substr(table.find("\n") + 1)),
              std::string::npos);
  }
}

TEST_F(ConfigHandlerTest, CustomKeymapZenzMigrationPreservesExistingRevert) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "zenz_space_existing_revert.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config config;
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tSpace\tConvertNext\n"
      "ZenzConversion\tSpace\tRevertZenzToMozc\n");
  ConfigHandler::SetConfig(config);
  const std::string table = ConfigHandler::GetCopiedConfig().custom_keymap_table();
  constexpr absl::string_view kRevert =
      "ZenzConversion\tSpace\tRevertZenzToMozc";
  const size_t first = table.find(kRevert);
  ASSERT_NE(first, std::string::npos);
  EXPECT_EQ(table.find(kRevert, first + 1), std::string::npos);
}

TEST_F(ConfigHandlerTest, LiveBackspaceMigrationAddsOnlyLiveRuleOnce) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "live_backspace_migration.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config config;
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tBackspace\tCancel\n"
      "Conversion\tSpace\tConvertNext\n");
  ConfigHandler::SetConfig(config);
  const std::string table = ConfigHandler::GetCopiedConfig().custom_keymap_table();
  constexpr absl::string_view live_row =
      "LiveConversion\tBackspace\tBackspace";
  constexpr absl::string_view zenz_row =
      "ZenzConversion\tBackspace\tBackspace";
  const size_t live_pos = table.find(live_row);
  ASSERT_NE(live_pos, std::string::npos);
  EXPECT_EQ(table.find(live_row, live_pos + 1), std::string::npos);
  // Zenz correction inherits LiveConversion (live origin) or Conversion
  // (manual origin). It must not receive a shared Backspace override.
  EXPECT_EQ(table.find(zenz_row), std::string::npos);
  ConfigHandler::SetConfig(ConfigHandler::GetCopiedConfig());
  EXPECT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(), table);
  ConfigHandler::Reload();
  EXPECT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(), table);
}

TEST_F(ConfigHandlerTest, LiveBackspaceMigrationPreservesExplicitOverrides) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "live_backspace_overrides.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);
  Config config;
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tBackspace\tCancel\n"
      "LiveConversion\tBackspace\tCommit\n");
  ConfigHandler::SetConfig(config);
  const std::string table = ConfigHandler::GetCopiedConfig().custom_keymap_table();
  EXPECT_NE(table.find("LiveConversion\tBackspace\tCommit"),
            std::string::npos);
  EXPECT_EQ(table.find("ZenzConversion\tBackspace\tBackspace"),
            std::string::npos);
  EXPECT_EQ(table.find("LiveConversion\tBackspace\tBackspace"),
            std::string::npos);
}

TEST_F(ConfigHandlerTest, LiveBackspaceMigrationWithExistingLiveBindingIsNoop) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "live_backspace_noop.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  // The missing terminal newline detects a no-op migration that needlessly
  // serializes a rewritten table without adding a new rule.
  // Suppress the independent Ctrl+Delete migration so that this test
  // exclusively exercises a no-op LiveConversion Backspace migration.
  const std::string original_table =
      "status\tkey\tcommand\n"
      "Conversion\tCtrl Delete\tDeleteSelectedCandidate\n"
      "Conversion\tBackspace\tCancel\n"
      "LiveConversion\tBackspace\tCommit";
  Config config;
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(original_table);
  ConfigHandler::SetConfig(config);
  ASSERT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(),
            original_table);

  // Verify that Reload() does not persist a needless migration.  A fixed
  // previous timestamp makes a spurious SetMetaData()/AtomicUpdate detectable.
  Config stored = ConfigHandler::GetCopiedConfig();
  stored.mutable_general_config()->set_last_modified_time(1000);
  const std::string saved_bytes = stored.SerializeAsString();
  {
    std::ofstream ofs(config_file, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(ofs);
    ofs.write(saved_bytes.data(), saved_bytes.size());
    ASSERT_TRUE(ofs);
  }
  ConfigHandler::Reload();
  EXPECT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(),
            original_table);
  std::ifstream ifs(config_file, std::ios::binary);
  ASSERT_TRUE(ifs);
  const std::string reloaded_bytes((std::istreambuf_iterator<char>(ifs)),
                                   std::istreambuf_iterator<char>());
  EXPECT_EQ(reloaded_bytes, saved_bytes);
}

TEST_F(ConfigHandlerTest, LiveBackspaceMigrationPreservesCustomZenzBinding) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "live_backspace_custom_zenz.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config config;
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(
      "status\tkey\tcommand\n"
      "Conversion\tBackspace\tCancel\n"
      "ZenzConversion\tBackspace\tCommit\n");
  ConfigHandler::SetConfig(config);
  const std::string migrated =
      ConfigHandler::GetCopiedConfig().custom_keymap_table();
  EXPECT_NE(migrated.find("LiveConversion\tBackspace\tBackspace"),
            std::string::npos);
  EXPECT_NE(migrated.find("ZenzConversion\tBackspace\tCommit"),
            std::string::npos);
  EXPECT_EQ(migrated.find("ZenzConversion\tBackspace\tBackspace"),
            std::string::npos);
  ConfigHandler::Reload();
  EXPECT_EQ(ConfigHandler::GetCopiedConfig().custom_keymap_table(), migrated);
}

TEST_F(ConfigHandlerTest, SwitchingConfigFilesDoesNotReuseOldHash) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string first_path =
      FileUtil::JoinPath(temp_dir.path(), "hash_first.db");
  const std::string second_path =
      FileUtil::JoinPath(temp_dir.path(), "hash_second.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(first_path));
  ASSERT_OK(FileUtil::UnlinkIfExists(second_path));

  ConfigHandler::SetConfigFileNameForTesting(first_path);
  Config input;
  input.set_incognito_mode(true);
  ConfigHandler::SetConfig(input);
  ASSERT_TRUE(ConfigHandler::GetCopiedConfig().incognito_mode());

  // The second file does not exist.  Its empty config must not inherit the
  // fingerprint of the previously saved file.
  ConfigHandler::SetConfigFileNameForTesting(second_path);
  ASSERT_FALSE(ConfigHandler::GetCopiedConfig().incognito_mode());
  ConfigHandler::SetConfig(input);
  EXPECT_TRUE(ConfigHandler::GetCopiedConfig().incognito_mode());
  std::ifstream ifs(second_path, std::ios::binary);
  ASSERT_TRUE(ifs);
  Config persisted;
  ASSERT_TRUE(persisted.ParseFromIstream(&ifs));
  EXPECT_TRUE(persisted.incognito_mode());
}

TEST_F(ConfigHandlerTest, ReloadUpdatesDeduplicationHashes) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "hash_external_reload.db");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);

  Config input;
  input.set_incognito_mode(true);
  ConfigHandler::SetConfig(input);
  ASSERT_TRUE(ConfigHandler::GetCopiedConfig().incognito_mode());

  // Simulate a different process rewriting the same config file.
  Config external = ConfigHandler::GetCopiedConfig();
  external.set_incognito_mode(false);
  const std::string external_bytes = external.SerializeAsString();
  {
    std::ofstream ofs(config_file, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(ofs);
    ofs.write(external_bytes.data(), external_bytes.size());
    ASSERT_TRUE(ofs);
  }
  ConfigHandler::Reload();
  ASSERT_FALSE(ConfigHandler::GetCopiedConfig().incognito_mode());

  // Restoring the former value must not be suppressed by a stale hash.
  ConfigHandler::SetConfig(input);
  EXPECT_TRUE(ConfigHandler::GetCopiedConfig().incognito_mode());
  std::ifstream ifs(config_file, std::ios::binary);
  ASSERT_TRUE(ifs);
  Config persisted;
  ASSERT_TRUE(persisted.ParseFromIstream(&ifs));
  EXPECT_TRUE(persisted.incognito_mode());
}

TEST_F(ConfigHandlerTest, MissingConfigUsesMozkeyProductDefaults) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "mozc_config_test_tmp");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);
  ConfigHandler::Reload();

  ExpectMozkeyProductDefaults(ConfigHandler::GetCopiedConfig());
}

TEST_F(ConfigHandlerTest, MozkeyProductDefaultsPreserveExplicitSettings) {
  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "mozc_config_test_tmp");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);
  ConfigHandler::Reload();

  Config input;
  input.set_use_live_conversion(false);
  input.set_show_candidate_window_on_initial_conversion(false);
  input.set_use_direct_commit(false);
  input.set_direct_commit_key(0);
  input.set_use_zenz_live_correction(false);
  input.set_use_zenz_feedback_learning(false);
  input.set_use_zenz_feedback_min_key_length(false);
  input.set_zenz_feedback_min_key_length(4);
  input.set_use_zenz_auto_block_rejected_correction(false);
  input.set_use_zenz_live_correction_right_context(false);
  input.set_use_realtime_conversion(true);
#ifdef _WIN32
  // Use values opposite to the product defaults so this test proves that
  // presence, not value equality, protects explicit user choices.
  input.set_dim_pending_roman_input(true);
  input.set_pending_roman_dimness_percent(40);
  input.set_use_custom_preedit_underline_color(false);
  input.set_preedit_underline_color(0x112233);
  input.set_use_custom_preedit_target_underline_color(false);
  input.set_preedit_target_underline_color(0x445566);
#endif  // _WIN32

  ConfigHandler::SetConfig(input);
  const Config output = ConfigHandler::GetCopiedConfig();

  EXPECT_FALSE(output.use_live_conversion());
  EXPECT_FALSE(output.show_candidate_window_on_initial_conversion());
  EXPECT_FALSE(output.use_direct_commit());
  EXPECT_EQ(output.direct_commit_key(), 0);
  EXPECT_FALSE(output.use_zenz_live_correction());
  EXPECT_FALSE(output.use_zenz_feedback_learning());
  EXPECT_FALSE(output.use_zenz_feedback_min_key_length());
  EXPECT_EQ(output.zenz_feedback_min_key_length(), 4);
  EXPECT_FALSE(output.use_zenz_auto_block_rejected_correction());
  EXPECT_FALSE(output.use_zenz_live_correction_right_context());
  EXPECT_TRUE(output.use_realtime_conversion());
#ifdef _WIN32
  EXPECT_TRUE(output.dim_pending_roman_input());
  EXPECT_EQ(output.pending_roman_dimness_percent(), 40);
  EXPECT_FALSE(output.use_custom_preedit_underline_color());
  EXPECT_EQ(output.preedit_underline_color(), 0x112233);
  EXPECT_FALSE(output.use_custom_preedit_target_underline_color());
  EXPECT_EQ(output.preedit_target_underline_color(), 0x445566);
#endif  // _WIN32
}

TEST_F(ConfigHandlerTest, SetMetadata) {
  auto make_Config_with_clock = [](int seconds, bool incognito) {
    Config input = ConfigHandler::DefaultConfig();
    input.set_incognito_mode(incognito);
    ClockMock clock(absl::FromUnixSeconds(seconds));
    Clock::SetClockForUnitTest(&clock);
    ConfigHandler::SetConfig(input);
    Clock::SetClockForUnitTest(nullptr);
    return ConfigHandler::GetCopiedConfig();
  };

  {
    const Config input1 = make_Config_with_clock(1000, false);
    const Config input2 = make_Config_with_clock(1000, false);
    const Config input3 = make_Config_with_clock(1001, false);

    // Don't update the config as long as the content is the same.
    EXPECT_EQ(absl::StrCat(input1), absl::StrCat(input2));
    EXPECT_EQ(absl::StrCat(input2), absl::StrCat(input3));
  }

  {
    const Config input1 = make_Config_with_clock(1000, true);
    const Config input2 = make_Config_with_clock(1000, false);
    const Config input3 = make_Config_with_clock(1001, true);

    EXPECT_EQ(input1.general_config().last_modified_time(), 1000);
    EXPECT_EQ(input2.general_config().last_modified_time(), 1000);
    EXPECT_EQ(input3.general_config().last_modified_time(), 1001);
  }
}

TEST_F(ConfigHandlerTest, SetConfig_IdentityCheck) {
  Config input;

  TempDirectory temp_dir = testing::MakeTempDirectoryOrDie();
  const std::string config_file =
      FileUtil::JoinPath(temp_dir.path(), "mozc_config_test_tmp");
  ASSERT_OK(FileUtil::UnlinkIfExists(config_file));
  ConfigHandler::SetConfigFileNameForTesting(config_file);
  EXPECT_EQ(ConfigHandler::GetConfigFileNameForTesting(), config_file);
  ConfigHandler::Reload();

  ConfigHandler::GetDefaultConfig(&input);
  input.set_incognito_mode(true);
#ifndef NDEBUG
  input.set_verbose_level(2);
#endif  // NDEBUG

  ClockMock clock1(absl::FromUnixSeconds(1000));

  Clock::SetClockForUnitTest(&clock1);
  ConfigHandler::SetConfig(input);
  std::shared_ptr<const config::Config> output1 =
      ConfigHandler::GetSharedConfig();

  ClockMock clock2(absl::FromUnixSeconds(1001));
  Clock::SetClockForUnitTest(&clock2);
  ConfigHandler::SetConfig(input);
  std::shared_ptr<const config::Config> output2 =
      ConfigHandler::GetSharedConfig();

  // As SetConfig() is called twice with the same config,
  // GetSharedConfig() must return the identical (including metadata!) config.
  // This also means no actual storage write access happened.
  EXPECT_EQ(absl::StrCat(*output1), absl::StrCat(*output2));
  Clock::SetClockForUnitTest(nullptr);
}

TEST_F(ConfigHandlerTest, ConfigFileNameConfig) {
  const std::string config_file = absl::StrCat("config", kConfigVersion, ".db");
  const std::string filename =
      FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(), config_file);
  ASSERT_OK(FileUtil::UnlinkIfExists(filename));
  Config input;
  ConfigHandler::SetConfig(input);
  EXPECT_OK(FileUtil::FileExists(filename));
}

TEST_F(ConfigHandlerTest, SetConfigFileName) {
  Config mozc_config;
  const bool default_incognito_mode = mozc_config.incognito_mode();
  mozc_config.set_incognito_mode(!default_incognito_mode);
  ConfigHandler::SetConfig(mozc_config);
  ConfigHandler::SetConfigFileNameForTesting(
      "memory://set_config_file_name_test.db");
  // After SetConfigFileName called, settings are set as default.
  EXPECT_EQ(ConfigHandler::GetSharedConfig()->incognito_mode(),
            default_incognito_mode);
}

#if !defined(__ANDROID__)
// Temporarily disable this test because FileUtil::CopyFile fails on
// Android for some reason.
TEST_F(ConfigHandlerTest, LoadTestConfig) {
  // TODO(yukawa): Generate test data automatically so that we can keep
  //     the compatibility among variety of config files.
  // TODO(yukawa): Enumerate test data in the directory automatically.
  constexpr std::array<absl::string_view, 3> kDataFiles = {
      "linux_config1.db",
      "mac_config1.db",
      "win_config1.db",
  };

  for (absl::string_view file_name : kDataFiles) {
    const std::string src_path = mozc::testing::GetSourceFileOrDie(
        {"data", "test", "config", file_name});
    const std::string dest_path =
        FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(), file_name);
    ASSERT_OK(FileUtil::CopyFile(src_path, dest_path))
        << "Copy failed: " << src_path << " to " << dest_path;

    ConfigHandler::SetConfigFileNameForTesting(
        absl::StrCat("user://", file_name));
    ConfigHandler::Reload();
  }
}
#endif  // !__ANDROID__

TEST_F(ConfigHandlerTest, GetDefaultConfig) {
  Config output;

  output.Clear();
  ConfigHandler::GetDefaultConfig(&output);
#ifdef __APPLE__
  EXPECT_EQ(output.session_keymap(), Config::KOTOERI);
#elif defined(OS_CHROMEOS)  // __APPLE__
  EXPECT_EQ(output.session_keymap(), Config::CHROMEOS);
#else   // __APPLE__ || OS_CHROMEOS
  EXPECT_EQ(output.session_keymap(), Config::MSIME);
#endif  // __APPLE__ || OS_CHROMEOS

  EXPECT_FALSE(output.has_use_live_conversion());
  EXPECT_FALSE(output.has_show_candidate_window_on_initial_conversion());
  EXPECT_FALSE(output.has_use_direct_commit());
  EXPECT_FALSE(output.has_direct_commit_key());
  EXPECT_FALSE(output.has_use_zenz_live_correction());
  EXPECT_FALSE(output.has_use_zenz_feedback_learning());
  EXPECT_FALSE(output.has_use_zenz_live_correction_right_context());
  EXPECT_FALSE(output.has_use_realtime_conversion());
  EXPECT_EQ(output.live_conversion_min_key_length(), 2);
  EXPECT_EQ(output.character_form_rules_size(), 13);

  struct TestCase {
    absl::string_view group;
    Config::CharacterForm preedit_character_form;
    Config::CharacterForm conversion_character_form;
  };

  constexpr std::array<TestCase, 13> kTestCases = {{
      // "ア"
      {"ア", Config::FULL_WIDTH, Config::FULL_WIDTH},
      {"A", Config::FULL_WIDTH, Config::LAST_FORM},
      {"0", Config::FULL_WIDTH, Config::LAST_FORM},
      {"(){}[]", Config::FULL_WIDTH, Config::LAST_FORM},
      {".,", Config::FULL_WIDTH, Config::LAST_FORM},
      // "。、",
      {"。、", Config::FULL_WIDTH, Config::FULL_WIDTH},
      // "・「」"
      {"・「」", Config::FULL_WIDTH, Config::FULL_WIDTH},
      {"\"'", Config::FULL_WIDTH, Config::LAST_FORM},
      {":;", Config::FULL_WIDTH, Config::LAST_FORM},
      {"#%&@$^_|`\\", Config::FULL_WIDTH, Config::LAST_FORM},
      {"~", Config::FULL_WIDTH, Config::LAST_FORM},
      {"<>=+-/*", Config::FULL_WIDTH, Config::LAST_FORM},
      {"?!", Config::FULL_WIDTH, Config::LAST_FORM},
  }};
  EXPECT_EQ(output.character_form_rules_size(), kTestCases.size());
  for (size_t i = 0; i < kTestCases.size(); ++i) {
    EXPECT_EQ(output.character_form_rules(i).group(), kTestCases[i].group);
    EXPECT_EQ(output.character_form_rules(i).preedit_character_form(),
              kTestCases[i].preedit_character_form);
    EXPECT_EQ(output.character_form_rules(i).conversion_character_form(),
              kTestCases[i].conversion_character_form);
  }
}

TEST_F(ConfigHandlerTest, ProductDefaultConfig) {
  ExpectMozkeyProductDefaults(ConfigHandler::GetProductDefaultConfig());
}

TEST_F(ConfigHandlerTest, DefaultConfig) {
  Config config;
  ConfigHandler::GetDefaultConfig(&config);
  EXPECT_EQ(absl::StrCat(ConfigHandler::DefaultConfig()), absl::StrCat(config));
}

// Returns concatenated serialized data of |Config::character_form_rules|.
std::string ExtractCharacterFormRules(const Config& config) {
  std::string rules;
  for (size_t i = 0; i < config.character_form_rules_size(); ++i) {
    EXPECT_TRUE(config.character_form_rules(i).AppendToString(&rules));
  }
  return rules;
}

TEST_F(ConfigHandlerTest, ConcurrentAccess) {
  std::vector<Config> configs;

  {
    Config config;
    ConfigHandler::GetDefaultConfig(&config);
    configs.push_back(config);
  }
  {
    Config config;
    ConfigHandler::GetDefaultConfig(&config);
    config.clear_character_form_rules();
    {
      auto* rule = config.add_character_form_rules();
      rule->set_group("0");
      rule->set_preedit_character_form(Config::HALF_WIDTH);
      rule->set_conversion_character_form(Config::HALF_WIDTH);
    }
    {
      auto* rule = config.add_character_form_rules();
      rule->set_group("A");
      rule->set_preedit_character_form(Config::LAST_FORM);
      rule->set_conversion_character_form(Config::LAST_FORM);
    }
    configs.push_back(config);
  }
  {
    Config config;
    ConfigHandler::GetDefaultConfig(&config);
    {
      auto* rule = config.add_character_form_rules();
      rule->set_group("0");
      rule->set_preedit_character_form(Config::HALF_WIDTH);
      rule->set_conversion_character_form(Config::HALF_WIDTH);
    }
    {
      auto* rule = config.add_character_form_rules();
      rule->set_group("A");
      rule->set_preedit_character_form(Config::LAST_FORM);
      rule->set_conversion_character_form(Config::LAST_FORM);
    }
    configs.push_back(config);
  }

  // Since |ConfigHandler::SetConfig()| actually updates some metadata in
  // |GeneralConfig|, the returned object from |ConfigHandler::GetConfig()|
  // is not predictable.  Hence we only make sure that
  // |Config::character_form_rules()| is one of expected values.
  absl::flat_hash_set<std::string> character_form_rules_set;
  for (const auto& config : configs) {
    character_form_rules_set.insert(ExtractCharacterFormRules(config));
  }

  // Before starting concurrent test, check to see if it works in single
  // thread.
  for (const auto& config : configs) {
    // Update the global config.
    ConfigHandler::SetConfig(config);

    // Check to see if the returned config contains one of expected
    // |Config::character_form_rules()|.
    const auto& rules =
        ExtractCharacterFormRules(ConfigHandler::GetCopiedConfig());
    ASSERT_TRUE(character_form_rules_set.contains(rules));
  }

  {
    absl::Notification cancel;

    std::vector<Thread> set_threads;
    for (int i = 0; i < 2; ++i) {
      set_threads.push_back(Thread([&cancel, &configs] {
        absl::BitGen gen;
        while (!cancel.HasBeenNotified()) {
          const size_t next_index = absl::Uniform(gen, 0u, configs.size());
          ConfigHandler::SetConfig(configs[next_index]);
        }
      }));
    }

    std::vector<Thread> get_threads;
    for (int i = 0; i < 4; ++i) {
      get_threads.push_back(Thread([&cancel, &character_form_rules_set] {
        while (!cancel.HasBeenNotified()) {
          const auto& rules =
              ExtractCharacterFormRules(ConfigHandler::GetCopiedConfig());
          EXPECT_TRUE(character_form_rules_set.contains(rules));
        }
      }));
    }

    // Wait for a while to see if everything goes well.
    // 250 msec is good enough to crash the code if it is not guarded by
    // the lock, but feel free to change the duration.  It is basically an
    // arbitrary number.
    absl::SleepFor(absl::Milliseconds(250));

    cancel.Notify();
    for (auto& set_thread : set_threads) {
      set_thread.Join();
    }
    for (auto& get_thread : get_threads) {
      get_thread.Join();
    }
  }
}

TEST_F(ConfigHandlerTest, GetSharedConfig) {
  auto config1 = ConfigHandler::GetSharedConfig();
  auto config2 = ConfigHandler::GetSharedConfig();
  EXPECT_EQ(config1, config2);

  Config config = *config1;
  config.set_incognito_mode(true);
  ConfigHandler::SetConfig(config);
  auto config3 = ConfigHandler::GetSharedConfig();
  EXPECT_NE(config1, config3);
  EXPECT_NE(config2, config3);

  auto config4 = ConfigHandler::GetSharedConfig();
  EXPECT_EQ(config3, config4);
}

}  // namespace
}  // namespace config
}  // namespace mozc
