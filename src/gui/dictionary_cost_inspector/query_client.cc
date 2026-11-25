// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#include "gui/dictionary_cost_inspector/query_client.h"

#include <cstdint>
#include <filesystem>
#include <string>

#include "base/system_util.h"
#include "base/util.h"
#include "ipc/ipc.h"
#include "protocol/commands.pb.h"
#include "protocol/dictionary_cost_limits.h"

namespace mozc::gui::cost_inspector {
namespace {

bool IsValidTarget(const QueryTarget& target) {
  if (target.channel.empty() || target.channel.size() > 120 ||
      target.server_path.empty() ||
      !std::filesystem::path(target.server_path).is_absolute()) {
    return false;
  }
  for (const char c : target.channel) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

bool IsValidRequest(absl::string_view reading, uint32_t max_entries,
                    absl::Duration timeout) {
  return !reading.empty() &&
         reading.size() <= commands::kDictionaryCostMaxReadingBytes &&
         reading.find('\0') == absl::string_view::npos &&
         Util::IsValidUtf8(reading) &&
         max_entries >= 1 &&
         max_entries <= commands::kDictionaryCostMaxEntriesPerSource &&
         timeout > absl::ZeroDuration() && timeout <= absl::Seconds(10);
}

}  // namespace

QueryTarget QueryTarget::Production() {
  // No access to the installed server is performed here.
  return {"session", SystemUtil::GetServerPath(), 0};
}

QueryOutcome QueryDictionaryCosts(const QueryTarget& target,
                                  absl::string_view reading,
                                  uint32_t max_entries_per_source,
                                  absl::Duration timeout) {
  QueryOutcome outcome;
  if (!IsValidRequest(reading, max_entries_per_source, timeout)) {
    outcome.status = QueryStatus::kInvalidInput;
    return outcome;
  }
  if (!IsValidTarget(target)) {
    outcome.status = QueryStatus::kInvalidTarget;
    return outcome;
  }

#ifdef _WIN32
  // Unlike client::Client, IPCClient does not auto-start or restart servers.
  // The nonempty absolute executable path pins the expected peer image.
  IPCClient client(target.channel, target.server_path);
  if (!client.Connected()) {
    outcome.status = client.GetLastIPCError() == IPC_INVALID_SERVER
                         ? QueryStatus::kPeerMismatch
                         : QueryStatus::kServerUnavailable;
    return outcome;
  }
  // Phase 3-B's Windows-only API uses the OS named-pipe handle, NOT the
  // potentially zero/stale PID in the IPC metadata file.
  const uint32_t peer_pid = client.GetConnectedServerProcessId();
  if (peer_pid == 0 ||
      (target.expected_pid != 0 && target.expected_pid != peer_pid)) {
    outcome.status = QueryStatus::kPeerMismatch;
    return outcome;  // Do not transmit the query to the wrong process.
  }
  outcome.connected_server_pid = peer_pid;

  commands::Input input;
  input.set_type(commands::Input::LOOKUP_DICTIONARY_COST_ENTRIES);
  auto* req = input.mutable_dictionary_cost_lookup_request();
  req->set_reading(std::string(reading.data(), reading.size()));
  req->set_max_entries_per_source(max_entries_per_source);
  std::string request;
  if (!input.SerializeToString(&request)) {
    outcome.status = QueryStatus::kMalformedResponse;
    return outcome;
  }
  std::string response;
  if (!client.Call(request, &response, timeout)) {
    outcome.status = QueryStatus::kTransportError;
    return outcome;
  }
  if (response.size() > commands::kDictionaryCostMaxResponseBytes + 4096) {
    outcome.status = QueryStatus::kMalformedResponse;
    return outcome;
  }
  commands::Output parsed;
  if (!parsed.ParseFromString(response)) {
    outcome.status = QueryStatus::kMalformedResponse;
    return outcome;
  }
  if (parsed.error_code() != commands::Output::SESSION_SUCCESS ||
      !parsed.has_dictionary_cost_lookup_result()) {
    // An old server may not recognize the command. Never restart it here.
    outcome.status = QueryStatus::kServerRejected;
    return outcome;
  }
  if (parsed.dictionary_cost_lookup_result().ByteSizeLong() >
      commands::kDictionaryCostMaxResponseBytes) {
    outcome.status = QueryStatus::kMalformedResponse;
    return outcome;
  }
  outcome.result = parsed.dictionary_cost_lookup_result();
  outcome.status = QueryStatus::kOk;
#else
  outcome.status = QueryStatus::kUnsupportedPlatform;
#endif
  return outcome;
}

QueryOutcome QueryRunningMozkey(absl::string_view reading,
                               uint32_t max_entries_per_source,
                               absl::Duration timeout) {
  return QueryDictionaryCosts(QueryTarget::Production(), reading,
                              max_entries_per_source, timeout);
}

}  // namespace mozc::gui::cost_inspector
