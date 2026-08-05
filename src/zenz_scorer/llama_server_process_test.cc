#include "zenz_scorer/llama_server_process.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "testing/gunit.h"

namespace mozc {
namespace zenz_scorer {
namespace {

std::string JoinPath(std::string_view left, std::string_view right) {
  if (left.empty()) {
    return std::string(right);
  }
  if (left.back() == '/') {
    return std::string(left) + std::string(right);
  }
  return std::string(left) + "/" + std::string(right);
}

std::string TestHelperPath() {
  const char* test_srcdir = std::getenv("TEST_SRCDIR");
  const char* test_workspace = std::getenv("TEST_WORKSPACE");
  if (test_srcdir == nullptr || test_workspace == nullptr) {
    return "";
  }
  return JoinPath(JoinPath(test_srcdir, test_workspace),
                  "zenz_scorer/llama_server_process_test_helper");
}

class TemporaryModel final {
 public:
  TemporaryModel() {
    char path[] = "/tmp/mozc_llama_process_model_XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd >= 0) {
      constexpr char kContents[] = "test model";
      const ssize_t ignored = ::write(fd, kContents, sizeof(kContents) - 1);
      static_cast<void>(ignored);
      ::close(fd);
      path_ = path;
    }
  }

  ~TemporaryModel() {
    if (!path_.empty()) {
      ::unlink(path_.c_str());
    }
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

bool IsLowerHex(std::string_view value) {
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

LlamaServerProcessOptions MakeOptions(const std::string& model_path) {
  LlamaServerProcessOptions options;
  options.executable_path = TestHelperPath();
  options.model_path = model_path;
  options.context_size = 128;
  options.threads = 2;
  options.readiness_timeout_msec = 3000;
  options.readiness_probe_interval_msec = 25;
  options.readiness_probe_timeout_msec = 250;
  return options;
}

TEST(LlamaServerProcessTest, StartsWithRandomEndpointAndStopsChild) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());
  ASSERT_FALSE(TestHelperPath().empty());

  LlamaServerProcess process(MakeOptions(model.path()));
  std::string error;
  ASSERT_TRUE(process.Start(&error)) << error;

  EXPECT_TRUE(process.running());
  EXPECT_GT(process.pid(), 0);
  EXPECT_GT(process.port(), 0);
  EXPECT_LE(process.port(), 65535);
  ASSERT_EQ(process.api_key().size(), 64);
  EXPECT_TRUE(IsLowerHex(process.api_key()));

  const pid_t child_pid = process.pid();
  std::string second_start_error;
  EXPECT_FALSE(process.Start(&second_start_error));
  EXPECT_EQ(second_start_error, "already_started");

  process.Stop();
  EXPECT_FALSE(process.running());
  EXPECT_EQ(process.pid(), -1);
  EXPECT_EQ(process.port(), 0);
  EXPECT_TRUE(process.api_key().empty());
  int status = 0;
  EXPECT_EQ(::waitpid(child_pid, &status, WNOHANG), -1);
  EXPECT_EQ(errno, ECHILD);
}

TEST(LlamaServerProcessTest, GeneratesNewApiKeyForEachLaunch) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  LlamaServerProcess process(MakeOptions(model.path()));
  std::string error;
  ASSERT_TRUE(process.Start(&error)) << error;
  const std::string first_key = process.api_key();
  process.Stop();

  ASSERT_TRUE(process.Start(&error)) << error;
  EXPECT_NE(process.api_key(), first_key);
  process.Stop();
}

TEST(LlamaServerProcessTest, ReadinessTimeoutTerminatesChild) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  LlamaServerProcessOptions options = MakeOptions(model.path());
  options.readiness_timeout_msec = 300;
  options.readiness_probe_interval_msec = 25;
  options.readiness_probe_timeout_msec = 100;
  options.additional_args = {"--test-mode=never-ready"};

  LlamaServerProcess process(std::move(options));
  std::string error;
  EXPECT_FALSE(process.Start(&error));
  EXPECT_NE(error.find("readiness_timeout:"), std::string::npos) << error;
  EXPECT_FALSE(process.running());
  EXPECT_EQ(process.pid(), -1);
  EXPECT_EQ(process.port(), 0);
  EXPECT_TRUE(process.api_key().empty());
}

TEST(LlamaServerProcessTest, ChildExitBeforeReadyIsReported) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  LlamaServerProcessOptions options = MakeOptions(model.path());
  options.additional_args = {"--test-mode=exit"};

  LlamaServerProcess process(std::move(options));
  std::string error;
  EXPECT_FALSE(process.Start(&error));
  EXPECT_NE(error.find("llama_server_exited_code_11"), std::string::npos)
      << error;
  EXPECT_FALSE(process.running());
}

TEST(LlamaServerProcessTest, CapturesBoundedRedactedStartupOutput) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  LlamaServerProcessOptions options = MakeOptions(model.path());
  options.additional_args = {"--test-mode=exit-with-stderr"};

  LlamaServerProcess process(std::move(options));
  std::string error;
  EXPECT_FALSE(process.Start(&error));
  EXPECT_NE(error.find("llama_server_exited_code_12"), std::string::npos)
      << error;
  EXPECT_NE(error.find("llama_server_output="), std::string::npos) << error;
  EXPECT_NE(error.find("api-key=<redacted>"), std::string::npos) << error;
  EXPECT_NE(error.find("diagnostic-tail"), std::string::npos) << error;
  EXPECT_EQ(error.find("diagnostic-start"), std::string::npos) << error;
  EXPECT_LE(error.size(), 4600);
  EXPECT_FALSE(process.running());
}

TEST(LlamaServerProcessTest, RejectsMissingRuntimeFiles) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  LlamaServerProcessOptions missing_executable = MakeOptions(model.path());
  missing_executable.executable_path = "/missing/llama-server";
  LlamaServerProcess first(std::move(missing_executable));
  std::string error;
  EXPECT_FALSE(first.Start(&error));
  EXPECT_EQ(error, "llama_server_not_found");

  LlamaServerProcessOptions missing_model = MakeOptions("/missing/model.gguf");
  LlamaServerProcess second(std::move(missing_model));
  EXPECT_FALSE(second.Start(&error));
  EXPECT_EQ(error, "model_not_found");
}

}  // namespace
}  // namespace zenz_scorer
}  // namespace mozc
