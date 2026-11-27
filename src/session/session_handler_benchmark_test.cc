

// Mozkey benchmark: desktop SessionHandler, using the embedded OSS dataset.
// No IPC, renderer, Zenz, or user-history learning is measured.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "config/config_handler.h"
#include "data_manager/oss/oss_data_manager.h"
#include "engine/engine.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/session_handler.h"
#include "testing/benchmark.h"

namespace mozc {
namespace {

constexpr std::string_view kInput = "nihongo";
constexpr std::string_view kReading = "にほんご";

bool Eval(SessionHandler& handler, commands::Command& command) {
  return handler.EvalCommand(&command) && command.has_output() &&
         command.output().error_code() == commands::Output::SESSION_SUCCESS;
}

bool ResetContext(SessionHandler& handler, uint64_t id) {
  commands::Command command;
  command.mutable_input()->set_type(commands::Input::SEND_COMMAND);
  command.mutable_input()->set_id(id);
  command.mutable_input()->mutable_command()->set_type(
      commands::SessionCommand::RESET_CONTEXT);
  return Eval(handler, command) && !command.output().has_preedit();
}

bool SendSpecial(SessionHandler& handler, uint64_t id,
                 commands::KeyEvent::SpecialKey key,
                 commands::Command& command) {
  command.Clear();
  auto* input = command.mutable_input();
  input->set_type(commands::Input::SEND_KEY);
  input->set_id(id);
  input->mutable_key()->set_special_key(key);
  return Eval(handler, command);
}

bool TypeRoman(SessionHandler& handler, uint64_t id,
               commands::Command& command) {
  for (char key : kInput) {
    command.Clear();
    auto* input = command.mutable_input();
    input->set_type(commands::Input::SEND_KEY);
    input->set_id(id);
    input->mutable_key()->set_key_code(key);
    if (!Eval(handler, command) || !command.output().consumed()) {
      return false;
    }
  }
  return true;
}

std::string Preedit(const commands::Command& command) {
  std::string text;
  for (const auto& segment : command.output().preedit().segment()) {
    text += segment.value();
  }
  return text;
}

void MeasureSession(benchmark::State& state, bool convert) {
  // gtest_main creates an isolated profile before running benchmarks.
  config::Config config;
  config::ConfigHandler::GetDefaultConfig(&config);
  config.set_preedit_method(config::Config::ROMAN);
  config.set_session_keymap(config::Config::MSIME);
  config.set_use_live_conversion(false);
  config.set_use_auto_conversion(false);
  config.set_use_zenz_live_correction(false);
  config.set_history_learning_level(config::Config::NO_HISTORY);
  config.set_use_history_suggest(false);
  config.set_incognito_mode(true);
  if (!config::ConfigHandler::SetConfig(config)) {
    state.SkipWithError("Isolated benchmark config could not be saved");
    return;
  }
  auto engine = Engine::CreateEngine(std::make_unique<oss::OssDataManager>());
  if (!engine.ok()) {
    state.SkipWithError("OSS engine creation failed");
    return;
  }
  SessionHandler handler(*std::move(engine));
  commands::Command command;
  command.mutable_input()->set_type(commands::Input::CREATE_SESSION);
  if (!Eval(handler, command) || command.output().id() == 0) {
    state.SkipWithError("Session creation failed");
    return;
  }
  const uint64_t id = command.output().id();
  if (!SendSpecial(handler, id, commands::KeyEvent::ON, command) ||
      !TypeRoman(handler, id, command) || Preedit(command) != kReading) {
    state.SkipWithError("Unexpected initial Roman input result");
    return;
  }
  if (convert &&
      (!SendSpecial(handler, id, commands::KeyEvent::SPACE, command) ||
       !command.output().consumed() || Preedit(command).empty() ||
       Preedit(command) == kReading)) {
    state.SkipWithError("Explicit conversion did not produce converted text");
    return;
  }
  for (auto _ : state) {
    state.PauseTiming();
    const bool reset = ResetContext(handler, id);
    state.ResumeTiming();
    if (!reset || !TypeRoman(handler, id, command)) {
      state.SkipWithError("Session reset or input failed");
      break;
    }
    if (convert &&
        (!SendSpecial(handler, id, commands::KeyEvent::SPACE, command) ||
         !command.output().consumed())) {
      state.SkipWithError("Explicit conversion failed");
      break;
    }
    state.PauseTiming();
    const std::string preedit = Preedit(command);
    const bool valid = convert ? !preedit.empty() && preedit != kReading
                               : preedit == kReading;
    state.ResumeTiming();
    if (!valid) {
      state.SkipWithError("Unexpected measured preedit");
      break;
    }
  }
  state.SetItemsProcessed(state.iterations() * (kInput.size() + (convert ? 1 : 0)));
  command.Clear();
  command.mutable_input()->set_type(commands::Input::DELETE_SESSION);
  command.mutable_input()->set_id(id);
  if (!Eval(handler, command)) {
    state.SkipWithError("Session deletion failed");
  }
}

void BM_SessionRomanInput(benchmark::State& state) {
  MeasureSession(state, false);
}
BENCHMARK(BM_SessionRomanInput);

void BM_SessionRomanInputAndConvert(benchmark::State& state) {
  MeasureSession(state, true);
}
BENCHMARK(BM_SessionRomanInputAndConvert);

}  // namespace
}  // namespace mozc
