#include "zenz_scorer/llama_server_process.h"

#if defined(_WIN32)
#error "llama_server_process.cc must not be built on Windows"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <crt_externs.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "zenz_scorer/llama_http_client.h"

namespace mozc {
namespace zenz_scorer {
namespace {

constexpr size_t kApiKeyBytes = 32;
constexpr int kMinimumContextSize = 64;
constexpr int kMaximumContextSize = 4096;
constexpr int kMinimumThreads = 1;
constexpr int kMaximumThreads = 128;
constexpr int kMinimumReadinessTimeoutMsec = 100;
constexpr int kMaximumReadinessTimeoutMsec = 10 * 60 * 1000;
constexpr int kMinimumProbeIntervalMsec = 10;
constexpr int kMaximumProbeIntervalMsec = 5000;
constexpr int kMinimumProbeTimeoutMsec = 50;
constexpr int kMaximumProbeTimeoutMsec = 10000;
constexpr int kGracefulStopTimeoutMsec = 2000;
constexpr int kStopPollIntervalMsec = 25;
constexpr char kReadinessPrompt[] =
    "\xEE\xB8\x82\xEE\xB8\x80\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88"
    "\xEE\xB8\x81";

bool ContainsNul(const std::string& value) {
  return value.find('\0') != std::string::npos;
}

bool ValidateExecutable(const std::string& path, std::string* error) {
  if (path.empty() || ContainsNul(path)) {
    *error = "invalid_executable_path";
    return false;
  }

  struct stat status = {};
  if (::stat(path.c_str(), &status) != 0) {
    *error = "llama_server_not_found";
    return false;
  }
  if (!S_ISREG(status.st_mode)) {
    *error = "llama_server_not_regular_file";
    return false;
  }
  if ((status.st_mode & (S_ISUID | S_ISGID)) != 0) {
    *error = "llama_server_setid_rejected";
    return false;
  }
  if (::access(path.c_str(), X_OK) != 0) {
    *error = "llama_server_not_executable";
    return false;
  }
  return true;
}

bool ValidateModel(const std::string& path, std::string* error) {
  if (path.empty() || ContainsNul(path)) {
    *error = "invalid_model_path";
    return false;
  }

  struct stat status = {};
  if (::stat(path.c_str(), &status) != 0) {
    *error = "model_not_found";
    return false;
  }
  if (!S_ISREG(status.st_mode)) {
    *error = "model_not_regular_file";
    return false;
  }
  return true;
}

bool ValidateOptions(const LlamaServerProcessOptions& options,
                     std::string* error) {
  if (!ValidateExecutable(options.executable_path, error) ||
      !ValidateModel(options.model_path, error)) {
    return false;
  }
  if (options.context_size < kMinimumContextSize ||
      options.context_size > kMaximumContextSize) {
    *error = "invalid_context_size";
    return false;
  }
  if (options.threads < kMinimumThreads || options.threads > kMaximumThreads) {
    *error = "invalid_threads";
    return false;
  }
  if (options.readiness_timeout_msec < kMinimumReadinessTimeoutMsec ||
      options.readiness_timeout_msec > kMaximumReadinessTimeoutMsec) {
    *error = "invalid_readiness_timeout";
    return false;
  }
  if (options.readiness_probe_interval_msec < kMinimumProbeIntervalMsec ||
      options.readiness_probe_interval_msec > kMaximumProbeIntervalMsec) {
    *error = "invalid_readiness_probe_interval";
    return false;
  }
  if (options.readiness_probe_timeout_msec < kMinimumProbeTimeoutMsec ||
      options.readiness_probe_timeout_msec > kMaximumProbeTimeoutMsec) {
    *error = "invalid_readiness_probe_timeout";
    return false;
  }
  for (const std::string& argument : options.additional_args) {
    if (ContainsNul(argument)) {
      *error = "invalid_additional_argument";
      return false;
    }
  }
  return true;
}

bool ReadRandomBytes(void* output, size_t size) {
  int fd;
  do {
    fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    return false;
  }

  uint8_t* current = static_cast<uint8_t*>(output);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t result = ::read(fd, current, remaining);
    if (result > 0) {
      current += result;
      remaining -= static_cast<size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    ::close(fd);
    return false;
  }

  ::close(fd);
  return true;
}

std::string GenerateApiKey() {
  std::array<uint8_t, kApiKeyBytes> bytes = {};
  if (!ReadRandomBytes(bytes.data(), bytes.size())) {
    return "";
  }

  constexpr char kHex[] = "0123456789abcdef";
  std::string output;
  output.resize(bytes.size() * 2);
  for (size_t i = 0; i < bytes.size(); ++i) {
    output[i * 2] = kHex[bytes[i] >> 4];
    output[i * 2 + 1] = kHex[bytes[i] & 0x0F];
  }
  return output;
}

int AllocateLoopbackPort(std::string* error) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    *error = "port_socket_failed";
    return 0;
  }

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;

  if (::bind(fd, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) != 0) {
    ::close(fd);
    *error = "port_bind_failed";
    return 0;
  }

  socklen_t address_size = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address),
                    &address_size) != 0) {
    ::close(fd);
    *error = "port_getsockname_failed";
    return 0;
  }

  const int port = static_cast<int>(ntohs(address.sin_port));
  ::close(fd);
  if (port <= 0) {
    *error = "port_allocation_failed";
    return 0;
  }
  return port;
}

std::vector<std::string> BuildArguments(
    const LlamaServerProcessOptions& options, int port,
    const std::string& api_key) {
  std::vector<std::string> arguments;
  arguments.reserve(14 + options.additional_args.size());
  arguments.push_back(options.executable_path);
  arguments.push_back("-m");
  arguments.push_back(options.model_path);
  arguments.push_back("-c");
  arguments.push_back(std::to_string(options.context_size));
  arguments.push_back("-t");
  arguments.push_back(std::to_string(options.threads));
  arguments.push_back("--host");
  arguments.push_back("127.0.0.1");
  arguments.push_back("--port");
  arguments.push_back(std::to_string(port));
  arguments.push_back("--api-key");
  arguments.push_back(api_key);
  arguments.insert(arguments.end(), options.additional_args.begin(),
                   options.additional_args.end());
  return arguments;
}

std::string ChildExitDebug(int status) {
  if (WIFEXITED(status)) {
    return "llama_server_exited_code_" + std::to_string(WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return "llama_server_signaled_" + std::to_string(WTERMSIG(status));
  }
  return "llama_server_exited";
}

}  // namespace

LlamaServerProcess::LlamaServerProcess(LlamaServerProcessOptions options)
    : options_(std::move(options)) {}

LlamaServerProcess::~LlamaServerProcess() { Stop(); }

bool LlamaServerProcess::Start(std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();

  if (pid_ > 0) {
    if (running()) {
      *error = "already_started";
      return false;
    }
    ResetState();
  }

  if (!ValidateOptions(options_, error)) {
    return false;
  }

  api_key_ = GenerateApiKey();
  if (api_key_.empty()) {
    *error = "api_key_generation_failed";
    ResetState();
    return false;
  }

  port_ = AllocateLoopbackPort(error);
  if (port_ == 0) {
    ResetState();
    return false;
  }

  if (!Spawn(error)) {
    ResetState();
    return false;
  }

  if (!WaitUntilReady(error)) {
    Stop();
    return false;
  }
  return true;
}

bool LlamaServerProcess::Spawn(std::string* error) {
  std::vector<std::string> arguments =
      BuildArguments(options_, port_, api_key_);
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1);
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  int result = ::posix_spawn_file_actions_init(&actions);
  if (result != 0) {
    *error = "spawn_file_actions_init_failed_" + std::to_string(result);
    return false;
  }

  result = ::posix_spawn_file_actions_addopen(
      &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  if (result == 0) {
    result = ::posix_spawn_file_actions_addopen(
        &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  }
  if (result != 0) {
    ::posix_spawn_file_actions_destroy(&actions);
    *error = "spawn_redirect_failed_" + std::to_string(result);
    return false;
  }

  posix_spawnattr_t attributes;
  result = ::posix_spawnattr_init(&attributes);
  if (result != 0) {
    ::posix_spawn_file_actions_destroy(&actions);
    *error = "spawn_attr_init_failed_" + std::to_string(result);
    return false;
  }

  short flags = POSIX_SPAWN_SETPGROUP;
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
  result = ::posix_spawnattr_setflags(&attributes, flags);
  if (result == 0) {
    result = ::posix_spawnattr_setpgroup(&attributes, 0);
  }
  if (result != 0) {
    ::posix_spawnattr_destroy(&attributes);
    ::posix_spawn_file_actions_destroy(&actions);
    *error = "spawn_attr_setup_failed_" + std::to_string(result);
    return false;
  }

  pid_t child_pid = -1;
  result = ::posix_spawn(&child_pid, options_.executable_path.c_str(), &actions,
                         &attributes, argv.data(), *_NSGetEnviron());

  ::posix_spawnattr_destroy(&attributes);
  ::posix_spawn_file_actions_destroy(&actions);

  if (result != 0 || child_pid <= 0) {
    *error = "posix_spawn_failed_" + std::to_string(result);
    return false;
  }

  pid_ = child_pid;
  return true;
}

bool LlamaServerProcess::WaitUntilReady(std::string* error) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(
                            options_.readiness_timeout_msec);
  std::string last_debug = "server_not_ready";

  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    if (ReapIfExited(&status)) {
      *error = ChildExitDebug(status);
      return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const int remaining_msec = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
            .count());
    if (remaining_msec <= 0) {
      break;
    }

    LlamaHttpCompletionRequest request;
    request.port = port_;
    request.api_key = api_key_;
    request.n_predict = 8;
    request.timeout_msec = static_cast<uint32_t>(std::min(
        options_.readiness_probe_timeout_msec, remaining_msec));
    request.max_output_chars = 8;
    request.prompt = kReadinessPrompt;

    const LlamaHttpCompletionResponse response =
        PostLlamaHttpCompletion(request);
    if (response.status == LlamaHttpCompletionStatus::kOk) {
      return true;
    }
    if (!response.debug.empty()) {
      last_debug = response.debug;
    }

    const auto after_probe = std::chrono::steady_clock::now();
    if (after_probe >= deadline) {
      break;
    }
    const auto sleep_duration = std::min(
        std::chrono::milliseconds(options_.readiness_probe_interval_msec),
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                               after_probe));
    if (sleep_duration.count() > 0) {
      std::this_thread::sleep_for(sleep_duration);
    }
  }

  int status = 0;
  if (ReapIfExited(&status)) {
    *error = ChildExitDebug(status);
    return false;
  }
  *error = "readiness_timeout:" + last_debug;
  return false;
}

bool LlamaServerProcess::ReapIfExited(int* status) {
  if (pid_ <= 0) {
    return true;
  }

  int local_status = 0;
  pid_t result;
  do {
    result = ::waitpid(pid_, &local_status, WNOHANG);
  } while (result < 0 && errno == EINTR);

  if (result == 0) {
    return false;
  }
  if (result == pid_ || (result < 0 && errno == ECHILD)) {
    pid_ = -1;
    if (status != nullptr) {
      *status = local_status;
    }
    return true;
  }
  return false;
}

bool LlamaServerProcess::running() {
  if (pid_ <= 0) {
    return false;
  }
  int status = 0;
  return !ReapIfExited(&status);
}

void LlamaServerProcess::Stop() {
  if (pid_ <= 0) {
    ResetState();
    return;
  }

  const pid_t child_pid = pid_;
  if (::kill(-child_pid, SIGTERM) != 0 && errno != ESRCH) {
    ::kill(child_pid, SIGTERM);
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kGracefulStopTimeoutMsec);
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    if (ReapIfExited(&status)) {
      ResetState();
      return;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kStopPollIntervalMsec));
  }

  if (::kill(-child_pid, SIGKILL) != 0 && errno != ESRCH) {
    ::kill(child_pid, SIGKILL);
  }

  int status = 0;
  pid_t result;
  do {
    result = ::waitpid(child_pid, &status, 0);
  } while (result < 0 && errno == EINTR);

  ResetState();
}

void LlamaServerProcess::ResetState() {
  pid_ = -1;
  port_ = 0;
  std::fill(api_key_.begin(), api_key_.end(), '\0');
  api_key_.clear();
}

}  // namespace zenz_scorer
}  // namespace mozc
