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

#include "renderer/renderer_server.h"

#include <atomic>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "config/config_handler.h"
#include "ipc/ipc.h"
#include "ipc/ipc_test_util.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/renderer_client.h"
#include "renderer/renderer_interface.h"
#include "renderer/renderer_style_handler.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace renderer {
namespace {

class TestRenderer : public RendererInterface {
 public:
  bool Activate() override { return true; }

  bool IsAvailable() const override { return true; }

  bool ExecCommand(const commands::RendererCommand& command) override {
    if (finished_.load(std::memory_order_acquire)) {
      return false;
    }
    counter_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  void Reset() { counter_.store(0, std::memory_order_relaxed); }

  int counter() const { return counter_.load(std::memory_order_relaxed); }

  void Shutdown() { finished_.store(true, std::memory_order_release); }

 private:
  std::atomic<int> counter_ = 0;
  std::atomic<bool> finished_ = false;
};

class TestRendererServer : public RendererServer {
 public:
  TestRendererServer() : RendererServer(true /* for_testing */) {}

  int StartMessageLoop() override { return 0; }

  // Not async for testing
  bool AsyncExecCommand(absl::string_view proto_message) override {
    commands::RendererCommand command;
    command.ParseFromString(proto_message);
    return ExecCommandInternal(command);
  }
};

// A renderer launcher which does nothing.
class DummyRendererLauncher : public RendererLauncherInterface {
 public:
  void StartRenderer(
      absl::string_view name, absl::string_view renderer_path,
      bool disable_renderer_path_check,
      IPCClientFactoryInterface* ipc_client_factory_interface) override {
    LOG(INFO) << name << " " << renderer_path;
  }

  bool ForceTerminateRenderer(absl::string_view name) override { return true; }

  void OnFatal(RendererErrorType type) override {
    LOG(ERROR) << static_cast<int>(type);
  }

  bool IsAvailable() const override { return true; }

  bool CanConnect() const override { return true; }

  void SetPendingCommand(const commands::RendererCommand& command) override {}

  void set_suppress_error_dialog(bool suppress) override {}
};

class RendererServerTest : public testing::TestWithTempUserProfile {};

TEST_F(RendererServerTest, UsesRubySpacingDefaultsWhenFieldsAreAbsent) {
  config::Config input = config::ConfigHandler::DefaultConfig();
  input.clear_ruby_window_horizontal_padding();
  input.clear_ruby_window_vertical_padding();
  input.clear_ruby_window_composition_gap();
  config::ConfigHandler::SetConfig(input);

  TestRendererServer server;

  const RendererStyleHandler::RubyWindowStyle style =
      RendererStyleHandler::GetRubyWindowStyle();
  EXPECT_EQ(14u, style.horizontal_padding);
  EXPECT_EQ(6u, style.vertical_padding);
  EXPECT_EQ(4u, style.composition_gap);
}

TEST_F(RendererServerTest, UsesFontWeightDefaultsWhenFieldsAreAbsent) {
  config::Config input = config::ConfigHandler::DefaultConfig();
  input.clear_candidate_window_font_weight();
  input.clear_suggest_window_font_weight();
  input.clear_ruby_window_font_weight();
  config::ConfigHandler::SetConfig(input);

  TestRendererServer server;

  RendererStyle candidate_style;
  RendererStyle suggestion_style;
  ASSERT_TRUE(RendererStyleHandler::GetRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kCandidate,
      &candidate_style));
  ASSERT_TRUE(RendererStyleHandler::GetRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kSuggestion,
      &suggestion_style));
  EXPECT_EQ(400, candidate_style.candidate_style().font_weight());
  EXPECT_EQ(400, suggestion_style.candidate_style().font_weight());
  EXPECT_EQ(400u, RendererStyleHandler::GetRubyWindowStyle().font_weight);
}

TEST_F(RendererServerTest, AppliesIndependentFontWeightsWithNormalization) {
  config::Config input = config::ConfigHandler::DefaultConfig();
  input.set_candidate_window_font_weight(149);
  input.set_suggest_window_font_weight(650);
  input.set_ruby_window_font_weight(999);
  config::ConfigHandler::SetConfig(input);

  TestRendererServer server;

  RendererStyle candidate_style;
  RendererStyle suggestion_style;
  ASSERT_TRUE(RendererStyleHandler::GetRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kCandidate,
      &candidate_style));
  ASSERT_TRUE(RendererStyleHandler::GetRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kSuggestion,
      &suggestion_style));
  EXPECT_EQ(100, candidate_style.candidate_style().font_weight());
  EXPECT_EQ(700, suggestion_style.candidate_style().font_weight());
  EXPECT_EQ(900u, RendererStyleHandler::GetRubyWindowStyle().font_weight);
}

TEST_F(RendererServerTest, AppliesRubySpacingConfigWithClamping) {
  config::Config input = config::ConfigHandler::DefaultConfig();
  input.set_ruby_window_horizontal_padding(999);
  input.set_ruby_window_vertical_padding(23);
  input.set_ruby_window_composition_gap(999);
  config::ConfigHandler::SetConfig(input);

  TestRendererServer server;

  const RendererStyleHandler::RubyWindowStyle style =
      RendererStyleHandler::GetRubyWindowStyle();
  EXPECT_EQ(40u, style.horizontal_padding);
  EXPECT_EQ(23u, style.vertical_padding);
  EXPECT_EQ(32u, style.composition_gap);
}

TEST_F(RendererServerTest, IPCTest) {
  mozc::IPCClientFactoryOnMemory on_memory_client_factory;

  auto server = std::make_unique<TestRendererServer>();
  TestRenderer renderer;
  server->SetRendererInterface(&renderer);
#ifdef __APPLE__
  server->SetMachPortManager(on_memory_client_factory.OnMemoryPortManager());
#endif  // __APPLE__
  renderer.Reset();

  // listening event
  server->StartServer();
  absl::SleepFor(absl::Seconds(1));

  DummyRendererLauncher launcher;
  std::unique_ptr<RendererClient> client = RendererClient::CreateForTesting(
      server->GetServiceName(), &on_memory_client_factory, &launcher,
      RendererClient::RendererPathCheckMode::DISABLED);

  commands::RendererCommand command;
  command.set_type(commands::RendererCommand::NOOP);

  // renderer is called via IPC
  client->ExecCommand(command);
  EXPECT_EQ(renderer.counter(), 1);

  client->ExecCommand(command);
  client->ExecCommand(command);
  client->ExecCommand(command);
  EXPECT_EQ(renderer.counter(), 4);

  // Gracefully shutdown the server.
  renderer.Shutdown();
  client->ExecCommand(command);
  server->Wait();
}

}  // namespace
}  // namespace renderer
}  // namespace mozc
