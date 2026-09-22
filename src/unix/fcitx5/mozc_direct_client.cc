#include "unix/fcitx5/mozc_direct_client.h"

#include <Fcitx5/Utils/fcitx-utils/macros.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "base/singleton.h"
#include "config/config_handler.h"
#include "data_manager/oss/oss_data_manager.h"
#include "engine/engine.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/key_info_util.h"
#include "session/session_handler.h"
#include "unix/fcitx5/mozc_client_interface.h"

namespace fcitx {

namespace {

std::unique_ptr<mozc::SessionHandler> CreateSessionHandler() {
  auto engine =
      mozc::Engine::CreateEngine(std::make_unique<mozc::oss::OssDataManager>());
  DCHECK_OK(engine);
  auto result =
      std::make_unique<mozc::SessionHandler>(std::move(engine.value()));
  return result;
}

mozc::SessionHandler* GetSessionHandler() {
  static std::unique_ptr<mozc::SessionHandler> g_session_handler =
      CreateSessionHandler();
  return g_session_handler.get();
}

}  // namespace

MozcDirectClient::MozcDirectClient() : id_(0) {
  // Initialize direct_mode_keys_
  direct_mode_keys_ = mozc::KeyInfoUtil::ExtractSortedDirectModeKeys(
      *mozc::config::ConfigHandler::GetSharedConfig());
  InitRequestForSvsJapanese(true);
}

MozcDirectClient::~MozcDirectClient() { DeleteSession(); }

void MozcDirectClient::InitRequestForSvsJapanese(bool use_svs) {
  request_ = std::make_unique<mozc::commands::Request>();

  mozc::commands::DecoderExperimentParams params;
  uint32_t variation_types = params.variation_character_types();
  if (use_svs) {
    variation_types |= mozc::commands::DecoderExperimentParams::SVS_JAPANESE;
  } else {
    variation_types &= ~mozc::commands::DecoderExperimentParams::SVS_JAPANESE;
  }
  request_->mutable_decoder_experiment_params()->set_variation_character_types(
      variation_types);
}

bool MozcDirectClient::EnsureSession() {
  if (server_status_ == SERVER_OK) {
    return true;
  }
  if (!CreateSession()) {
    LOG(ERROR) << "CreateSession failed";
    return false;
  }

  // Call SET_REQUEST if request_ is not nullptr.
  if (request_) {
    mozc::commands::Input input;
    input.set_id(id_);
    input.set_type(mozc::commands::Input::SET_REQUEST);
    *input.mutable_request() = *request_;
    mozc::commands::Output output;
    Call(input, &output);
  }

  server_status_ = SERVER_OK;
  return true;
}

bool MozcDirectClient::SendKeyWithContext(
    const mozc::commands::KeyEvent& key, const mozc::commands::Context& context,
    mozc::commands::Output* output) {
  mozc::commands::Input input;
  input.set_type(mozc::commands::Input::SEND_KEY);
  *input.mutable_key() = key;
  // If the pointer of |context| is not the default_instance, update the data.
  if (&context != &mozc::commands::Context::default_instance()) {
    *input.mutable_context() = context;
  }
  return EnsureCallCommand(&input, output);
}

bool MozcDirectClient::SendCommandWithContext(
    const mozc::commands::SessionCommand& command,
    const mozc::commands::Context& context, mozc::commands::Output* output) {
  mozc::commands::Input input;
  input.set_type(mozc::commands::Input::SEND_COMMAND);
  *input.mutable_command() = command;
  // If the pointer of |context| is not the default_instance, update the data.
  if (&context != &mozc::commands::Context::default_instance()) {
    *input.mutable_context() = context;
  }
  return EnsureCallCommand(&input, output);
}

bool MozcDirectClient::EnsureCallCommand(mozc::commands::Input* input,
                                         mozc::commands::Output* output) {
  if (!EnsureSession()) {
    LOG(ERROR) << "EnsureSession failed";
    return false;
  }
  InitInput(input);
  output->set_id(0);

  if (!Call(*input, output)) {
    LOG(ERROR) << "Call command failed";
    return false;
  }
  return true;
}

void MozcDirectClient::set_client_capability(
    const mozc::commands::Capability& capability) {
  client_capability_ = capability;
}

bool MozcDirectClient::CreateSession() {
  id_ = 0;
  mozc::commands::Input input;
  input.set_type(mozc::commands::Input::CREATE_SESSION);

  *input.mutable_capability() = client_capability_;

  mozc::commands::Output output;
  if (!Call(input, &output)) {
    return false;
  }

  if (output.error_code() != mozc::commands::Output::SESSION_SUCCESS) {
    LOG(ERROR) << "Server returns an error";
    server_status_ = SERVER_INVALID_SESSION;
    return false;
  }

  id_ = output.id();
  return true;
}

bool MozcDirectClient::DeleteSession() {
  // No need to delete session
  if (id_ == 0) {
    return true;
  }

  mozc::commands::Input input;
  InitInput(&input);
  input.set_type(mozc::commands::Input::DELETE_SESSION);

  mozc::commands::Output output;
  if (!Call(input, &output)) {
    LOG(ERROR) << "DeleteSession failed";
    return false;
  }
  id_ = 0;
  return true;
}

bool MozcDirectClient::IsDirectModeCommand(
    const mozc::commands::KeyEvent& key) const {
  return mozc::KeyInfoUtil::ContainsKey(direct_mode_keys_, key);
}

bool MozcDirectClient::GetConfig(mozc::config::Config* config) {
  mozc::commands::Input input;
  InitInput(&input);
  input.set_type(mozc::commands::Input::GET_CONFIG);

  mozc::commands::Output output;
  if (!Call(input, &output)) {
    return false;
  }

  if (!output.has_config()) {
    return false;
  }

  config->Clear();
  *config = output.config();
  return true;
}

bool MozcDirectClient::SyncData() {
  return CallCommand(mozc::commands::Input::SYNC_DATA);
}

bool MozcDirectClient::CallCommand(mozc::commands::Input::CommandType type) {
  mozc::commands::Input input;
  InitInput(&input);
  input.set_type(type);
  mozc::commands::Output output;
  return Call(input, &output);
}

bool MozcDirectClient::Call(const mozc::commands::Input& input,
                            mozc::commands::Output* output) {
  mozc::commands::Command command;
  *command.mutable_input() = input;
  if (!GetSessionHandler()->EvalCommand(&command)) {
    return false;
  }
  *output = command.output();
  return true;
}

void MozcDirectClient::InitInput(mozc::commands::Input* input) const {
  input->set_id(id_);
}

bool MozcDirectClient::TranslateProtoBufToMozcToolArg(
    const mozc::commands::Output& output, std::string* mode) {
  if (!output.has_launch_tool_mode() || mode == nullptr) {
    return false;
  }

  switch (output.launch_tool_mode()) {
    case mozc::commands::Output::CONFIG_DIALOG:
      mode->assign("config_dialog");
      break;
    case mozc::commands::Output::DICTIONARY_TOOL:
      mode->assign("dictionary_tool");
      break;
    case mozc::commands::Output::WORD_REGISTER_DIALOG:
      mode->assign("word_register_dialog");
      break;
    case mozc::commands::Output::NO_TOOL:
    default:
      // do nothing
      return false;
      break;
  }

  return true;
}

bool MozcDirectClient::LaunchToolWithProtoBuf(
    const mozc::commands::Output& output) {
  std::string mode;
  if (!TranslateProtoBufToMozcToolArg(output, &mode)) {
    return false;
  }

  // TODO(nona): extends output message to support extra argument.
  return LaunchTool(mode, "");
}

bool MozcDirectClient::LaunchTool(const std::string& mode,
                                  const std::string_view extra_arg) {
  FCITX_UNUSED(mode);
  FCITX_UNUSED(extra_arg);
  // We don't spawn a server thread as for now, so the tool is not helpful
  // anyway.
  return false;
}

std::unique_ptr<MozcClientInterface> createClient() {
  return std::make_unique<MozcDirectClient>();
}

}  // namespace fcitx
