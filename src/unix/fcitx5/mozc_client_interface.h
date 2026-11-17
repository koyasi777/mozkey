#ifndef UNIX_FCITX5_MOZC_CLIENT_INTERFACE_H_
#define UNIX_FCITX5_MOZC_CLIENT_INTERFACE_H_

#include <memory>
#include <string>
#include <string_view>

#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"

namespace fcitx {

// This is a simplified version of mozc::ClientInterface, with only functions
// Needed by Fcitx.
class MozcClientInterface {
 public:
  virtual ~MozcClientInterface() = default;
  virtual bool EnsureConnection() = 0;
  bool SendCommand(const mozc::commands::SessionCommand& command,
                   mozc::commands::Output* output) {
    return SendCommandWithContext(
        command, mozc::commands::Context::default_instance(), output);
  }
  virtual bool SendKeyWithContext(const mozc::commands::KeyEvent& key,
                                  const mozc::commands::Context& context,
                                  mozc::commands::Output* output) = 0;
  virtual bool SendCommandWithContext(
      const mozc::commands::SessionCommand& command,
      const mozc::commands::Context& context,
      mozc::commands::Output* output) = 0;
  virtual bool IsDirectModeCommand(
      const mozc::commands::KeyEvent& key) const = 0;
  virtual bool GetConfig(mozc::config::Config* config) = 0;
  virtual void set_client_capability(
      const mozc::commands::Capability& capability) = 0;
  virtual bool SyncData() = 0;
  virtual bool LaunchTool(const std::string& mode, std::string_view arg) = 0;
  virtual bool LaunchToolWithProtoBuf(const mozc::commands::Output& output) = 0;
};

std::unique_ptr<MozcClientInterface> createClient();

}  // namespace fcitx
#endif
