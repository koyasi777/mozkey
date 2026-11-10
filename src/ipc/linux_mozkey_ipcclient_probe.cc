// Copyright 2026 Mozkey contributors.
// Use of this source code is governed by the BSD-style license.
//
// P2-b4: A test-only client using Mozkey's actual production IPCClient.
// The server is supplied by a separate test harness in a disposable HOME.
// No server auto-launch, IBus registration or configuration mutation occurs.

#include <iostream>
#include <string>

#include "absl/time/time.h"
#include "base/system_util.h"
#include "ipc/ipc.h"
#include "protocol/commands.pb.h"

int main(int argc, char** argv) {
#if !defined(__linux__) || defined(__ANDROID__) || defined(OS_CHROMEOS)
  std::cerr << "Linux desktop probe only\n";
  return 2;
#else
  if (argc != 3) {
    std::cerr << "Usage: probe expected_server_exe accept|reject\n";
    return 2;
  }
  // This is the canonical path that the production Client's launcher uses.
  if (mozc::SystemUtil::GetServerPath() != "/usr/lib/mozkey/mozc_server") {
    std::cerr << "Unexpected canonical server path\n";
    return 3;
  }

  const std::string mode(argv[2]);
  if (mode != "accept" && mode != "reject") {
    std::cerr << "Unsupported mode\n";
    return 2;
  }
  const std::string expected_path(argv[1]);
  if (expected_path.empty() || expected_path.front() != '/') {
    std::cerr << "Refuse an empty/relative expected server path\n";
    return 2;
  }

  mozc::IPCClient client("session", expected_path);
  if (mode == "reject") {
    // Must specifically be rejected by the production Linux peer path check,
    // rather than failing due to an absent socket or a different error.
    if (client.Connected() || client.GetLastIPCError() != mozc::IPC_INVALID_SERVER) {
      std::cerr << "Unexpected result for wrong server executable path: "
                << static_cast<int>(client.GetLastIPCError()) << "\n";
      return 4;
    }
    std::cout << "PRODUCTION_IPCCLIENT=REJECTED_WRONG_EXECUTABLE_PATH\n";
    return 0;
  }

  if (!client.Connected() || client.GetLastIPCError() != mozc::IPC_NO_ERROR) {
    std::cerr << "Production IPCClient could not connect: "
              << static_cast<int>(client.GetLastIPCError()) << "\n";
    return 5;
  }
  mozc::commands::Input input;
  input.set_type(mozc::commands::Input::NO_OPERATION);
  std::string request;
  if (!input.SerializeToString(&request)) {
    std::cerr << "Input serialization failed\n";
    return 6;
  }
  std::string reply;
  if (!client.Call(request, &reply, absl::Seconds(7))) {
    std::cerr << "Production IPCClient NO_OPERATION failed: "
              << static_cast<int>(client.GetLastIPCError()) << "\n";
    return 7;
  }
  mozc::commands::Output output;
  if (!output.ParseFromString(reply) ||
      output.error_code() != mozc::commands::Output::SESSION_SUCCESS ||
      output.id() != 0) {
    std::cerr << "Unexpected server NO_OPERATION response\n";
    return 8;
  }
  std::cout << "PRODUCTION_IPCCLIENT=ACCEPTED_AND_NO_OPERATION_OK\n";
  return 0;
#endif
}
