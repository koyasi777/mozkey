#include "unix/fcitx5/mozc_ipc_client.h"

#include <memory>
#include <string>
#include <string_view>

#include "client/client.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "unix/fcitx5/mozc_client_interface.h"

namespace fcitx {

MozcIPCClient::MozcIPCClient()
    : client_(mozc::client::ClientFactory::NewClient()) {}

MozcIPCClient::~MozcIPCClient() {}

bool MozcIPCClient::EnsureConnection() { return client_->EnsureConnection(); }

bool MozcIPCClient::SendKeyWithContext(const mozc::commands::KeyEvent& key,
                                       const mozc::commands::Context& context,
                                       mozc::commands::Output* output) {
  return client_->SendKeyWithContext(key, context, output);
}

bool MozcIPCClient::SendCommandWithContext(
    const mozc::commands::SessionCommand& command,
    const mozc::commands::Context& context, mozc::commands::Output* output) {
  return client_->SendCommandWithContext(command, context, output);
}
bool MozcIPCClient::IsDirectModeCommand(
    const mozc::commands::KeyEvent& key) const {
  return client_->IsDirectModeCommand(key);
}
bool MozcIPCClient::GetConfig(mozc::config::Config* config) {
  return client_->GetConfig(config);
}
void MozcIPCClient::set_client_capability(
    const mozc::commands::Capability& capability) {
  return client_->set_client_capability(capability);
}
bool MozcIPCClient::SyncData() { return client_->SyncData(); }
bool MozcIPCClient::LaunchTool(const std::string& mode, std::string_view arg) {
  return client_->LaunchTool(mode, arg);
}
bool MozcIPCClient::LaunchToolWithProtoBuf(
    const mozc::commands::Output& output) {
  return client_->LaunchToolWithProtoBuf(output);
}

std::unique_ptr<MozcClientInterface> createClient() {
  return std::make_unique<MozcIPCClient>();
}

}  // namespace fcitx
