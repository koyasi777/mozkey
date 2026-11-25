// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
// Unit tests use *only* a private non-running channel. No production IPC.
#ifdef _WIN32
#include <windows.h>

#include <cstdint>
#include <string>

#include "absl/time/time.h"
#include "base/win32/wide_char.h"
#include "gui/dictionary_cost_inspector/query_client.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::gui::cost_inspector {
namespace {

class QueryClientTest : public testing::TestWithTempUserProfile {};

QueryTarget UnusedPrivateTarget() {
  wchar_t exe[32768] = {};
  const DWORD length = ::GetModuleFileNameW(nullptr, exe, 32768);
  if (length == 0 || length >= 32768) return {};
  return {"session_cost4a_unused_" + std::to_string(::GetCurrentProcessId()) +
              "_" + std::to_string(::GetTickCount64()),
          win32::WideToUtf8(std::wstring(exe, length)), 0};
}

TEST_F(QueryClientTest, InvalidInputDoesNotConnect) {
  const QueryTarget target = UnusedPrivateTarget();
  ASSERT_FALSE(target.server_path.empty());
  EXPECT_EQ(QueryDictionaryCosts(target, "", 1, absl::Seconds(1)).status,
            QueryStatus::kInvalidInput);
  EXPECT_EQ(QueryDictionaryCosts(target, "\xFF", 1, absl::Seconds(1)).status,
            QueryStatus::kInvalidInput);
  EXPECT_EQ(QueryDictionaryCosts(target, "きょうと", 0, absl::Seconds(1)).status,
            QueryStatus::kInvalidInput);
  EXPECT_EQ(QueryDictionaryCosts(target, "きょうと", 257, absl::Seconds(1)).status,
            QueryStatus::kInvalidInput);
  EXPECT_EQ(QueryDictionaryCosts(target, "きょうと", 1, absl::ZeroDuration()).status,
            QueryStatus::kInvalidInput);
}

TEST_F(QueryClientTest, MissingOrRelativeServerPathIsRejected) {
  QueryTarget target = UnusedPrivateTarget();
  target.server_path.clear();
  EXPECT_EQ(QueryDictionaryCosts(target, "きょうと", 1, absl::Seconds(1)).status,
            QueryStatus::kInvalidTarget);
  target.server_path = "relative_server.exe";
  EXPECT_EQ(QueryDictionaryCosts(target, "きょうと", 1, absl::Seconds(1)).status,
            QueryStatus::kInvalidTarget);
}

TEST_F(QueryClientTest, MissingServerFailsClosedWithoutLaunchingAnything) {
  const QueryTarget target = UnusedPrivateTarget();
  ASSERT_FALSE(target.server_path.empty());
  const QueryOutcome outcome =
      QueryDictionaryCosts(target, "きょうと", 1, absl::Seconds(1));
  EXPECT_EQ(outcome.status, QueryStatus::kServerUnavailable);
  EXPECT_EQ(outcome.connected_server_pid, 0u);
  EXPECT_FALSE(outcome.ok());
  // This test only connects to its random private channel; no server is run.
}

}  // namespace
}  // namespace mozc::gui::cost_inspector
#endif  // _WIN32
