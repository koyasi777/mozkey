// Copyright 2026
// Licensed under the same terms as Mozc.

#include "session/zenz_context_request.h"

#include "gtest/gtest.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"

namespace mozc {
namespace session {
namespace {

config::Config EnabledConfig() {
  config::Config config;
  config.set_use_live_conversion(true);
  config.set_use_zenz_live_correction(true);
  return config;
}

TEST(ZenzContextRequestTest, DisabledZenzRequestsNothing) {
  config::Config config;
  config.set_use_live_conversion(true);
  config.set_use_zenz_live_correction(false);

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, true);

  EXPECT_EQ(request.preceding_length, 0);
  EXPECT_EQ(request.following_length, 0);
}

TEST(ZenzContextRequestTest, DisabledLiveConversionRequestsNothing) {
  config::Config config;
  config.set_use_live_conversion(false);
  config.set_use_zenz_live_correction(true);

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, true);

  EXPECT_EQ(request.preceding_length, 0);
  EXPECT_EQ(request.following_length, 0);
}

TEST(ZenzContextRequestTest, NonSnapshotStateRequestsNothing) {
  config::Config config = EnabledConfig();

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, false);

  EXPECT_EQ(request.preceding_length, 0);
  EXPECT_EQ(request.following_length, 0);
}

TEST(ZenzContextRequestTest, PasswordRequestsNothing) {
  config::Config config = EnabledConfig();
  config.set_use_zenz_live_correction_right_context(true);

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::PASSWORD, true);

  EXPECT_EQ(request.preceding_length, 0);
  EXPECT_EQ(request.following_length, 0);
}

TEST(ZenzContextRequestTest, UsesEffectiveDefaultLengths) {
  config::Config config = EnabledConfig();

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, true);

  EXPECT_EQ(request.preceding_length, 24);
  EXPECT_EQ(request.following_length, 10);
}

TEST(ZenzContextRequestTest, DisabledRightContextRequestsOnlyPreceding) {
  config::Config config = EnabledConfig();
  config.set_use_zenz_live_correction_right_context(false);

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, true);

  EXPECT_EQ(request.preceding_length, 24);
  EXPECT_EQ(request.following_length, 0);
}

TEST(ZenzContextRequestTest, ExplicitlyEnabledRightContextUsesFallbackLength) {
  config::Config config = EnabledConfig();
  config.set_use_zenz_live_correction_right_context(true);

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, true);

  EXPECT_EQ(request.preceding_length, 24);
  EXPECT_EQ(request.following_length, 10);
}

TEST(ZenzContextRequestTest, ClampsBothDirectionsForAcquisition) {
  config::Config config = EnabledConfig();
  config.set_zenz_live_correction_left_context_length(4096);
  config.set_use_zenz_live_correction_right_context(true);
  config.set_zenz_live_correction_right_context_length(4096);

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, true);

  EXPECT_EQ(request.preceding_length, 128);
  EXPECT_EQ(request.following_length, 128);
}

TEST(ZenzContextRequestTest, ExplicitZeroLengthsRemainZero) {
  config::Config config = EnabledConfig();
  config.set_zenz_live_correction_left_context_length(0);
  config.set_use_zenz_live_correction_right_context(true);
  config.set_zenz_live_correction_right_context_length(0);

  const ZenzContextRequest request =
      GetZenzContextRequest(config, commands::Context::NORMAL, true);

  EXPECT_EQ(request.preceding_length, 0);
  EXPECT_EQ(request.following_length, 0);
}

TEST(ZenzContextRequestTest, AttachUsesPresenceOnlyForNonzeroLengths) {
  commands::Output output;
  ZenzContextRequest request;
  request.preceding_length = 24;
  request.following_length = 10;

  AttachZenzContextRequest(request, &output);

  ASSERT_TRUE(output.has_zenz_preceding_text_request_length());
  EXPECT_EQ(output.zenz_preceding_text_request_length(), 24);
  ASSERT_TRUE(output.has_zenz_following_text_request_length());
  EXPECT_EQ(output.zenz_following_text_request_length(), 10);

  AttachZenzContextRequest(ZenzContextRequest(), &output);

  EXPECT_FALSE(output.has_zenz_preceding_text_request_length());
  EXPECT_FALSE(output.has_zenz_following_text_request_length());
}

TEST(ZenzContextRequestTest, NullOutputIsAccepted) {
  ZenzContextRequest request;
  request.preceding_length = 24;
  AttachZenzContextRequest(request, nullptr);
}

}  // namespace
}  // namespace session
}  // namespace mozc