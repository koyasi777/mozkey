#include "zenz/zenz_unix_socket_path.h"

#include <string>

#if defined(__linux__)
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#endif  // __linux__

#include "testing/gunit.h"

namespace mozc {
namespace zenz {
namespace {

#if defined(__linux__)
class ScopedXdgRuntimeDir final {
 public:
  explicit ScopedXdgRuntimeDir(const char* new_value) {
    const char* existing = ::getenv("XDG_RUNTIME_DIR");
    if (existing != nullptr) {
      had_original_ = true;
      original_ = existing;
    }
    if (new_value == nullptr) {
      ::unsetenv("XDG_RUNTIME_DIR");
    } else {
      ::setenv("XDG_RUNTIME_DIR", new_value, 1);
    }
  }
  ~ScopedXdgRuntimeDir() {
    if (had_original_) {
      ::setenv("XDG_RUNTIME_DIR", original_.c_str(), 1);
    } else {
      ::unsetenv("XDG_RUNTIME_DIR");
    }
  }
  ScopedXdgRuntimeDir(const ScopedXdgRuntimeDir&) = delete;
  ScopedXdgRuntimeDir& operator=(const ScopedXdgRuntimeDir&) = delete;

 private:
  bool had_original_ = false;
  std::string original_;
};

class ScopedTestRuntimeRoot final {
 public:
  ScopedTestRuntimeRoot() {
    char pattern[] = "/tmp/mozkey-z1a-runtime-XXXXXX";
    const char* const created = ::mkdtemp(pattern);
    if (created != nullptr) {
      path_ = created;
    }
  }
  ~ScopedTestRuntimeRoot() {
    if (!path_.empty()) {
      ::chmod(path_.c_str(), 0700);
      ::rmdir(path_.c_str());
    }
  }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};
#endif  // __linux__

TEST(ZenzUnixSocketPathTest, RejectsEmptyAndEmbeddedNullPaths) {
  EXPECT_FALSE(IsValidZenzUnixSocketPath(""));

  const std::string embedded_null("/tmp/mozc\0zenz", 14);
  EXPECT_FALSE(IsValidZenzUnixSocketPath(embedded_null));
}

TEST(ZenzUnixSocketPathTest, ReturnsPlatformEndpoint) {
#if defined(__linux__)
  ScopedTestRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  ScopedXdgRuntimeDir environment(root.path().c_str());
#endif  // __linux__
  const std::string directory = GetZenzUnixSocketDirectory();
  const std::string socket_path = GetZenzUnixSocketPath();

#if defined(_WIN32)
  EXPECT_TRUE(directory.empty());
  EXPECT_TRUE(socket_path.empty());
#else
  EXPECT_FALSE(directory.empty());
  EXPECT_FALSE(socket_path.empty());
#if defined(__linux__)
  EXPECT_EQ(directory, root.path() + "/mozkey-zenz");
#else
  EXPECT_EQ(directory.find("/tmp/mozc_zenz_"), 0);
#endif
  EXPECT_EQ(socket_path, directory + "/scorer.sock");
  EXPECT_TRUE(IsValidZenzUnixSocketPath(socket_path));
#endif  // _WIN32
}

#if defined(__linux__)
TEST(ZenzUnixSocketPathTest, LinuxRejectsAbsentOrRelativeRuntimeRoot) {
  {
    ScopedXdgRuntimeDir environment(nullptr);
    EXPECT_TRUE(GetZenzUnixSocketDirectory().empty());
    EXPECT_TRUE(GetZenzUnixSocketPath().empty());
  }
  {
    ScopedXdgRuntimeDir environment("relative/run/user");
    EXPECT_TRUE(GetZenzUnixSocketDirectory().empty());
    EXPECT_TRUE(GetZenzUnixSocketPath().empty());
  }
}

TEST(ZenzUnixSocketPathTest, LinuxRejectsInsecureMode) {
  ScopedTestRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  ASSERT_EQ(::chmod(root.path().c_str(), 0755), 0);
  ScopedXdgRuntimeDir environment(root.path().c_str());
  EXPECT_TRUE(GetZenzUnixSocketDirectory().empty());
  EXPECT_TRUE(GetZenzUnixSocketPath().empty());
}

TEST(ZenzUnixSocketPathTest, LinuxRejectsSymlinkRuntimeRoot) {
  ScopedTestRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  const std::string link = root.path() + "-link";
  ASSERT_EQ(::symlink(root.path().c_str(), link.c_str()), 0);
  {
    ScopedXdgRuntimeDir environment(link.c_str());
    EXPECT_TRUE(GetZenzUnixSocketDirectory().empty());
    EXPECT_TRUE(GetZenzUnixSocketPath().empty());
  }
  EXPECT_EQ(::unlink(link.c_str()), 0);
}

TEST(ZenzUnixSocketPathTest, LinuxRejectsDotComponents) {
  ScopedTestRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  const std::string dot = root.path() + "/.";
  ScopedXdgRuntimeDir environment(dot.c_str());
  EXPECT_TRUE(GetZenzUnixSocketDirectory().empty());
  EXPECT_TRUE(GetZenzUnixSocketPath().empty());
}

TEST(ZenzUnixSocketPathTest, LinuxRejectsOverlongSocketPath) {
  ScopedTestRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  const std::string long_root = root.path() + "/" + std::string(90, 'x');
  ASSERT_EQ(::mkdir(long_root.c_str(), 0700), 0);
  {
    ScopedXdgRuntimeDir environment(long_root.c_str());
    EXPECT_FALSE(GetZenzUnixSocketDirectory().empty());
    EXPECT_TRUE(GetZenzUnixSocketPath().empty());
  }
  EXPECT_EQ(::rmdir(long_root.c_str()), 0);
}

TEST(ZenzUnixSocketPathTest, LinuxNormalizesTrailingSlash) {
  ScopedTestRuntimeRoot root;
  ASSERT_FALSE(root.path().empty());
  const std::string with_slash = root.path() + "/";
  ScopedXdgRuntimeDir environment(with_slash.c_str());
  EXPECT_EQ(GetZenzUnixSocketDirectory(), root.path() + "/mozkey-zenz");
  EXPECT_EQ(GetZenzUnixSocketPath(),
            root.path() + "/mozkey-zenz/scorer.sock");
}
#endif  // __linux__

TEST(ZenzUnixSocketPathTest, IsDeterministicWithinProcess) {
  EXPECT_EQ(GetZenzUnixSocketDirectory(), GetZenzUnixSocketDirectory());
  EXPECT_EQ(GetZenzUnixSocketPath(), GetZenzUnixSocketPath());
}

}  // namespace
}  // namespace zenz
}  // namespace mozc
