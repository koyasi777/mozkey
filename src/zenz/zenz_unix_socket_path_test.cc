#include "zenz/zenz_unix_socket_path.h"

#include <string>

#include "testing/gunit.h"

namespace mozc {
namespace zenz {
namespace {

TEST(ZenzUnixSocketPathTest, RejectsEmptyAndEmbeddedNullPaths) {
  EXPECT_FALSE(IsValidZenzUnixSocketPath(""));

  const std::string embedded_null("/tmp/mozc\0zenz", 14);
  EXPECT_FALSE(IsValidZenzUnixSocketPath(embedded_null));
}

TEST(ZenzUnixSocketPathTest, ReturnsPlatformEndpoint) {
  const std::string directory = GetZenzUnixSocketDirectory();
  const std::string socket_path = GetZenzUnixSocketPath();

#if defined(_WIN32)
  EXPECT_TRUE(directory.empty());
  EXPECT_TRUE(socket_path.empty());
#else
  EXPECT_FALSE(directory.empty());
  EXPECT_FALSE(socket_path.empty());
  EXPECT_EQ(socket_path, directory + "/scorer.sock");
  EXPECT_TRUE(IsValidZenzUnixSocketPath(socket_path));
#endif  // _WIN32
}

TEST(ZenzUnixSocketPathTest, IsDeterministicWithinProcess) {
  EXPECT_EQ(GetZenzUnixSocketDirectory(), GetZenzUnixSocketDirectory());
  EXPECT_EQ(GetZenzUnixSocketPath(), GetZenzUnixSocketPath());
}

}  // namespace
}  // namespace zenz
}  // namespace mozc
