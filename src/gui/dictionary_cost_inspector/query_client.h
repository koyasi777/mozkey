// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZC_GUI_DICTIONARY_COST_INSPECTOR_QUERY_CLIENT_H_
#define MOZC_GUI_DICTIONARY_COST_INSPECTOR_QUERY_CLIENT_H_

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "protocol/commands.pb.h"

namespace mozc::gui::cost_inspector {

// No server launcher, reloader, session creation or shutdown is linked here.
// A target identifies a *currently running* server. Test-only callers may
// provide a private channel and the exact path/PID of their own child server.
struct QueryTarget {
  std::string channel;
  std::string server_path;
  uint32_t expected_pid = 0;  // 0: require a real PID, but do not pin its value.

  // Configuration only: does not connect or start a server.
  static QueryTarget Production();
};

enum class QueryStatus {
  kOk,
  kInvalidInput,
  kInvalidTarget,
  kServerUnavailable,
  kPeerMismatch,
  kTransportError,
  kMalformedResponse,
  kServerRejected,
  kUnsupportedPlatform,
};

struct QueryOutcome {
  QueryStatus status = QueryStatus::kInvalidTarget;
  uint32_t connected_server_pid = 0;
  commands::DictionaryCostLookupResult result;

  bool ok() const { return status == QueryStatus::kOk; }
};

// Synchronous IPC; call from a worker thread, NEVER the GUI event loop.
// This API does not start, restart, stop, or persist anything in Mozkey.
// A 5-second bounded Call timeout is recommended. IPCClient construction may
// also wait for a busy named pipe; this is NOT a whole-operation deadline.
QueryOutcome QueryDictionaryCosts(const QueryTarget& target,
                                  absl::string_view reading,
                                  uint32_t max_entries_per_source,
                                  absl::Duration timeout);

// The product-facing entry point intentionally has no arbitrary IPC target.
QueryOutcome QueryRunningMozkey(absl::string_view reading,
                               uint32_t max_entries_per_source,
                               absl::Duration timeout = absl::Seconds(5));

}  // namespace mozc::gui::cost_inspector

#endif  // MOZC_GUI_DICTIONARY_COST_INSPECTOR_QUERY_CLIENT_H_
