#include <csignal>
#include <iostream>
#include <string>

#include "zenz/zenz_unix_socket_path.h"
#include "zenz/zenz_wire_protocol.h"
#include "zenz_scorer/posix_server.h"

namespace {

volatile std::sig_atomic_t g_shutdown_requested = 0;

void HandleSignal(int) { g_shutdown_requested = 1; }

mozc::zenz_scorer::PosixZenzResponse RuntimeUnavailable(
    const mozc::zenz_scorer::PosixZenzRequest&) {
  mozc::zenz_scorer::PosixZenzResponse response;
  response.status = mozc::zenz::kZenzWireStatusError;
  response.debug = "llama_backend_not_implemented";
  return response;
}

}  // namespace

int main() {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  mozc::zenz_scorer::PosixZenzScorerServer server(
      mozc::zenz::GetZenzUnixSocketPath(), RuntimeUnavailable);
  std::string error;
  if (!server.Start(&error)) {
    std::cerr << "[mozc-zenz-scorer] start failed: " << error << '\n';
    return 1;
  }

  while (g_shutdown_requested == 0) {
    if (!server.ServeOne(250, &error)) {
      std::cerr << "[mozc-zenz-scorer] serve failed: " << error << '\n';
      return 1;
    }
  }
  return 0;
}
