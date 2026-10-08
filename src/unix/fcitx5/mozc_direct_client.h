#ifndef UNIX_FCITX5_MOZC_DIRECT_CLIENT_H_
#define UNIX_FCITX5_MOZC_DIRECT_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "composer/key_event_util.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "unix/fcitx5/mozc_client_interface.h"

namespace fcitx {

class MozcDirectClient : public MozcClientInterface {
 public:
  MozcDirectClient();
  ~MozcDirectClient();

  bool EnsureConnection() override { return true; }
  bool SendKeyWithContext(const mozc::commands::KeyEvent& key,
                          const mozc::commands::Context& context,
                          mozc::commands::Output* output) override;
  bool SendCommandWithContext(const mozc::commands::SessionCommand& command,
                              const mozc::commands::Context& context,
                              mozc::commands::Output* output) override;
  bool IsDirectModeCommand(const mozc::commands::KeyEvent& key) const override;
  bool GetConfig(mozc::config::Config* config) override;
  void set_client_capability(
      const mozc::commands::Capability& capability) override;
  bool SyncData() override;
  bool LaunchTool(const std::string& mode, std::string_view arg) override;
  bool LaunchToolWithProtoBuf(const mozc::commands::Output& output) override;

 private:
  // Initializes `request_` with the flag.
  // This function should be called before EnsureSession.
  void InitRequestForSvsJapanese(bool use_svs);
  // Converts Output message from server to corresponding mozc_tool arguments
  // If launch_tool_mode is not set or NO_TOOL is set or an invalid value is
  // set, this function will return false and do nothing.
  static bool TranslateProtoBufToMozcToolArg(
      const mozc::commands::Output& output, std::string* mode);

  bool EnsureSession();

  enum ServerStatus {
    SERVER_INVALID_SESSION,  // current session is not available
    SERVER_OK,               // both server and session are health
  };

  // Initialize input filling id and preferences.
  void InitInput(mozc::commands::Input* input) const;

  bool CreateSession();
  bool DeleteSession();
  bool CallCommand(mozc::commands::Input::CommandType type);

  // This method re-issue session id if it is not available.
  bool EnsureCallCommand(mozc::commands::Input* input,
                         mozc::commands::Output* output);

  // The most primitive Call method
  bool Call(const mozc::commands::Input& input, mozc::commands::Output* output);

  uint64_t id_;
  std::unique_ptr<mozc::commands::Request> request_;
  ServerStatus server_status_ = SERVER_INVALID_SESSION;
  // List of key combinations used in the direct input mode.
  std::vector<mozc::KeyInformation> direct_mode_keys_;
  mozc::commands::Capability client_capability_;
};
}  // namespace fcitx

#endif
