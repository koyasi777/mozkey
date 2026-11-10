// Copyright 2026 Mozkey contributors.
// Use of this source code is governed by the BSD-style license.

#include <algorithm>
#include <cstddef>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "base/const.h"
#include "base/hash.h"
#include "base/system_util.h"
#include "ipc/ipc_path_manager.h"
#include "ipc/named_event.h"
#include "testing/gunit.h"

namespace mozc {
namespace {

#if defined(__linux__) && !defined(__ANDROID__) && !defined(OS_CHROMEOS)

// Mirrors the current POSIX sem_open name derivation in named_event.cc.
// This is a negative test for collision with the original Mozc namespace.
std::string SemaphoreNameForIdentity(const std::string& identity) {
  std::string name = absl::StrFormat("/%x", CityFingerprint(identity));
  name.resize(std::min<size_t>(13, name.size()));
  return name;
}

TEST(LinuxMozkeyNamespaceTest, NamedEventDoesNotShareMozcSemaphore) {
  EXPECT_STREQ(kEventPathPrefix, "mozkey.event.");
  const std::string uid = SystemUtil::GetUserSidAsString();
  const std::string suffix = absl::StrCat(uid, ".", "linux_namespace_test");
  const std::string mozkey_identity = absl::StrCat("mozkey.event.", suffix);
  const std::string mozc_identity = absl::StrCat("mozc.event.", suffix);

  const std::string actual = NamedEventUtil::GetEventPath("linux_namespace_test");
  EXPECT_EQ(actual, SemaphoreNameForIdentity(mozkey_identity));
  EXPECT_NE(actual, SemaphoreNameForIdentity(mozc_identity));
}

TEST(LinuxMozkeyNamespaceTest, AbstractSocketDoesNotUseMozcPrefix) {
  IPCPathManager* manager =
      IPCPathManager::GetIPCPathManager("linux_namespace_test");
  ASSERT_TRUE(manager->CreateNewPathName());
  std::string socket_name;
  ASSERT_TRUE(manager->GetPathName(&socket_name));

  // The initial slash is NUL in Linux's abstract Unix socket namespace.
  constexpr char kMozkeyPrefix[] = "\0tmp/.mozkey.";
  constexpr char kMozcPrefix[] = "\0tmp/.mozc.";
  const std::string expected_prefix(kMozkeyPrefix, sizeof(kMozkeyPrefix) - 1);
  const std::string legacy_prefix(kMozcPrefix, sizeof(kMozcPrefix) - 1);
  ASSERT_GE(socket_name.size(), expected_prefix.size());
  EXPECT_EQ(socket_name.substr(0, expected_prefix.size()), expected_prefix);
  EXPECT_NE(socket_name.substr(0, legacy_prefix.size()), legacy_prefix);
}

#endif  // Linux Mozkey

}  // namespace
}  // namespace mozc
