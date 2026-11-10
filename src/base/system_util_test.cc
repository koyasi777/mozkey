// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "base/system_util.h"

#include <string>

#include "base/environ_mock.h"
#include "base/file_util.h"
#include "base/file_util_mock.h"
#include "testing/gmock.h"
#include "testing/gunit.h"

namespace mozc {

class SystemUtilTest : public testing::Test {};

TEST_F(SystemUtilTest, GetUserProfileDirectory) {
#if defined(OS_CHROMEOS)
  EXPECT_EQ(SystemUtil::GetUserProfileDirectory(), "/mutable");

#elif defined(__wasm__)
  EXPECT_EQ(SystemUtil::GetUserProfileDirectory(), "/tmp");

#elif defined(__ANDROID__)
  EXPECT_TRUE(SystemUtil::GetUserProfileDirectory().empty());

#elif defined(_WIN32)
#elif defined(__APPLE__)
  // TODO(komatsu): write a test.

#elif defined(__linux__)
  EnvironMock environ_mock;
  FileUtilMock file_util_mock;
  SystemUtil::SetUserProfileDirectory("");

  // The default path is "$HOME/.config/mozkey".
  EXPECT_FALSE(FileUtil::DirectoryExists("/home/mozcuser/.config/mozkey").ok());
  EXPECT_EQ("/home/mozcuser/.config/mozkey",
            SystemUtil::GetUserProfileDirectory());
  EXPECT_OK(FileUtil::DirectoryExists("/home/mozcuser/.config/mozkey"));

  environ_mock.SetEnv("XDG_CONFIG_HOME", "/tmp/config");

  // Once the dir is initialized, the value is not changed.
  EXPECT_EQ("/home/mozcuser/.config/mozkey",
            SystemUtil::GetUserProfileDirectory());

  // Resets the cache by setting an empty string.
  SystemUtil::SetUserProfileDirectory("");

  // $XDG_CONFIG_HOME is already set to "/tmp/config" above.
  EXPECT_FALSE(FileUtil::DirectoryExists("/tmp/config/mozkey").ok());
  EXPECT_EQ("/tmp/config/mozkey", SystemUtil::GetUserProfileDirectory());
  EXPECT_OK(FileUtil::DirectoryExists("/tmp/config/mozkey"));

  // Resets again.
  SystemUtil::SetUserProfileDirectory("");

  // A pre-existing Mozc legacy directory must not redirect Mozkey's profile.
  EXPECT_OK(FileUtil::CreateDirectory("/home/mozcuser/.mozc"));
  EXPECT_OK(FileUtil::DirectoryExists("/home/mozcuser/.mozc"));
  environ_mock.SetEnv("XDG_CONFIG_HOME", "");
  EXPECT_EQ("/home/mozcuser/.config/mozkey",
            SystemUtil::GetUserProfileDirectory());
  EXPECT_OK(FileUtil::DirectoryExists("/home/mozcuser/.config/mozkey"));

  // Resets again to avoid side effects to other tests.
  SystemUtil::SetUserProfileDirectory("");

#else  // Platforms
#error Undefined target platform.

#endif  // Platforms
}

#if defined(__linux__) && !defined(__ANDROID__)
TEST_F(SystemUtilTest, LinuxInstallationPathsAreMozkeySpecific) {
  EXPECT_EQ(SystemUtil::GetServerDirectory(), "/usr/lib/mozkey");
  EXPECT_EQ(SystemUtil::GetServerPath(), "/usr/lib/mozkey/mozc_server");
  EXPECT_EQ(SystemUtil::GetRendererPath(), "/usr/lib/mozkey/mozc_renderer");
  EXPECT_EQ(SystemUtil::GetToolPath(), "/usr/lib/mozkey/mozc_tool");
  EXPECT_EQ(SystemUtil::GetDocumentDirectory(), "/usr/lib/mozkey/documents");
}
#endif  // __linux__ && !__ANDROID__

TEST_F(SystemUtilTest, GetTotalPhysicalMemoryTest) {
  EXPECT_GT(SystemUtil::GetTotalPhysicalMemory(), 0);
}

#ifdef __ANDROID__
TEST_F(SystemUtilTest, GetOSVersionStringTestForAndroid) {
  std::string result = SystemUtil::GetOSVersionString();
  // |result| must start with "Android ".
  EXPECT_TRUE(result.starts_with("Android "));
}
#endif  // __ANDROID__

}  // namespace mozc
