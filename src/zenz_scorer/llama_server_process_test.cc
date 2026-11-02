#include "zenz_scorer/llama_server_process.h"

#include <cerrno>
#include <chrono>
#include <thread>
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

std::string GuardPath() {
  const char* test_srcdir = std::getenv("TEST_SRCDIR");
  const char* test_workspace = std::getenv("TEST_WORKSPACE");
  if (test_srcdir == nullptr || test_workspace == nullptr) {
    return "";
  }
  return JoinPath(JoinPath(test_srcdir, test_workspace),
                  "zenz_scorer/mozc_zenz_guard");
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
  options.guard_executable_path = GuardPath();
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

TEST(LlamaServerProcessTest, DisablesGlobalPromptCacheWhenRequested) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  LlamaServerProcessOptions options = MakeOptions(model.path());
  options.disable_global_prompt_cache = true;
  options.additional_args = {"--test-mode=require-cache-ram-zero"};

  LlamaServerProcess process(std::move(options));
  std::string error;
  ASSERT_TRUE(process.Start(&error)) << error;
  EXPECT_TRUE(process.running());
  process.Stop();
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

TEST(LlamaServerProcessTest, GuardOwnsWorkerAndEscalatesTermIgnoringWorker) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());
  LlamaServerProcessOptions options = MakeOptions(model.path());
  options.additional_args = {"--test-mode=ignore-term"};
  LlamaServerProcess process(std::move(options));
  std::string error;
  ASSERT_TRUE(process.Start(&error)) << error;
  ASSERT_GT(process.pid(), 0);
  ASSERT_GT(process.worker_pid(), 0);
  EXPECT_NE(process.pid(), process.worker_pid());
  const pid_t worker_pid = process.worker_pid();
  const auto started = std::chrono::steady_clock::now();
  process.Stop();
  EXPECT_LT(std::chrono::steady_clock::now() - started,
            std::chrono::seconds(5));
  EXPECT_EQ(process.worker_pid(), -1);
  EXPECT_EQ(::kill(worker_pid, 0), -1);
  EXPECT_EQ(errno, ESRCH);
}

TEST(LlamaServerProcessTest, ParentSigkillCausesGuardToReapWorker) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());
  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);
  const pid_t owner = ::fork();
  ASSERT_GE(owner, 0);
  if (owner == 0) {
    ::close(fds[0]);
    LlamaServerProcess process(MakeOptions(model.path()));
    std::string error;
    const pid_t worker = process.Start(&error) ? process.worker_pid() : -1;
    const ssize_t ignored = ::write(fds[1], &worker, sizeof(worker));
    static_cast<void>(ignored);
    ::close(fds[1]);
    if (worker <= 0) {
      ::_exit(3);
    }
    while (true) {
      ::pause();
    }
  }
  ::close(fds[1]);
  pid_t worker = -1;
  const ssize_t got = ::read(fds[0], &worker, sizeof(worker));
  ::close(fds[0]);
  // The only SIGKILL in this test targets the owner created by this test.
  ::kill(owner, SIGKILL);
  int owner_status = 0;
  ASSERT_EQ(::waitpid(owner, &owner_status, 0), owner);
  ASSERT_EQ(got, sizeof(worker));
  ASSERT_GT(worker, 0);
  bool exited = false;
  for (int i = 0; i < 120; ++i) {
    if (::kill(worker, 0) == -1 && errno == ESRCH) {
      exited = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT_TRUE(exited) << "worker was not reaped after owner SIGKILL";
}

TEST(LlamaServerProcessTest, GuardSigkillCausesWorkerToExit) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  LlamaServerProcess process(MakeOptions(model.path()));
  std::string error;
  ASSERT_TRUE(process.Start(&error)) << error;
  const pid_t guard_pid = process.pid();
  const pid_t worker_pid = process.worker_pid();
  ASSERT_GT(guard_pid, 0);
  ASSERT_GT(worker_pid, 0);
  ASSERT_NE(guard_pid, worker_pid);

  // Kill only the guard started by this test; the scorer remains alive.
  ASSERT_EQ(::kill(guard_pid, SIGKILL), 0);
  bool worker_exited = false;
  for (int i = 0; i < 120; ++i) {
    if (::kill(worker_pid, 0) == -1 && errno == ESRCH) {
      worker_exited = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  process.Stop();  // Reap the killed guard; do not kill any process by name.
  EXPECT_TRUE(worker_exited)
      << "worker survived guard SIGKILL despite FD24 lifetime EOF";
}

TEST(LlamaServerProcessTest, RejectsKeyOverrideArgument) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());
  LlamaServerProcessOptions options = MakeOptions(model.path());
  options.additional_args = {"--api-key", "not-allowed"};
  LlamaServerProcess process(std::move(options));
  std::string error;
  EXPECT_FALSE(process.Start(&error));
  EXPECT_EQ(error, "api_key_override_rejected");
}

TEST(LlamaServerProcessTest, RejectsNetworkEndpointOverrideArgument) {
  TemporaryModel model;
  ASSERT_FALSE(model.path().empty());

  for (const std::string& argument : {
           "--host", "--host=0.0.0.0", "--port", "--port=18080"}) {
    LlamaServerProcessOptions options = MakeOptions(model.path());
    options.additional_args = {argument};
    LlamaServerProcess process(std::move(options));
    std::string error;
    EXPECT_FALSE(process.Start(&error)) << argument;
    EXPECT_EQ(error, "network_endpoint_override_rejected") << argument;
  }
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

  LlamaServerProcessOptions missing_guard = MakeOptions(model.path());
  missing_guard.guard_executable_path = "/missing/mozc_zenz_guard";
  LlamaServerProcess third(std::move(missing_guard));
  EXPECT_FALSE(third.Start(&error));
  EXPECT_EQ(error, "guard_llama_server_not_found");

  LlamaServerProcessOptions missing_model = MakeOptions("/missing/model.gguf");
  LlamaServerProcess second(std::move(missing_model));
  EXPECT_FALSE(second.Start(&error));
  EXPECT_EQ(error, "model_not_found");
}

}  // namespace
}  // namespace zenz_scorer
}  // namespace mozc
