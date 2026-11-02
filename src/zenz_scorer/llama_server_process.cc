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
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <crt_externs.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "zenz_scorer/llama_http_client.h"
#include "zenz_scorer/llama_server_guard_protocol.h"

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
constexpr int kMaximumProbeTimeoutMsec = 30000;
constexpr int kStopPollIntervalMsec = 25;
constexpr size_t kMaximumCapturedOutputBytes = 4096;
constexpr int kGuardStartupTimeoutMsec = 5000;
constexpr int kGuardShutdownTimeoutMsec = 4500;
constexpr char kReadinessPrompt[] =
    "\xEE\xB8\x82\xEE\xB8\x80\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88"
    "\xEE\xB8\x81";


bool SetCloseOnExec(int fd) {
  int flags;
  do {
    flags = ::fcntl(fd, F_GETFD);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) {
    return false;
  }

  int result;
  do {
    result = ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  } while (result < 0 && errno == EINTR);
  return result == 0;
}

bool MoveAboveStandardDescriptors(int* fd) {
  if (*fd > STDERR_FILENO) {
    return SetCloseOnExec(*fd);
  }

  int duplicate;
  do {
    duplicate = ::fcntl(*fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
  } while (duplicate < 0 && errno == EINTR);
  if (duplicate < 0) {
    return false;
  }

  ::close(*fd);
  *fd = duplicate;
  return true;
}

bool CreateOutputPipe(int output_pipe[2], std::string* error) {
  if (::pipe(output_pipe) != 0) {
    *error = "output_pipe_failed";
    return false;
  }

  if (!MoveAboveStandardDescriptors(&output_pipe[0]) ||
      !MoveAboveStandardDescriptors(&output_pipe[1])) {
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);
    output_pipe[0] = -1;
    output_pipe[1] = -1;
    *error = "output_pipe_setup_failed";
    return false;
  }
  return true;
}

// A fixed, explicit FD mapping avoids dependence on inherited process state.
// Sources above 63 cannot be overwritten by destination FDs 20 and 21.
bool MoveFdToGuardSource(int* fd) {
  int copy;
  do {
    copy = ::fcntl(*fd, F_DUPFD_CLOEXEC, 64);
  } while (copy < 0 && errno == EINTR);
  if (copy < 0) {
    return false;
  }
  ::close(*fd);
  *fd = copy;
  return true;
}

void ClosePipe(int fd[2]) {
  for (int i = 0; i < 2; ++i) {
    if (fd[i] >= 0) {
      ::close(fd[i]);
      fd[i] = -1;
    }
  }
}

// Startup is always bounded. The guard sends one fixed-size atomic message
// only after it has created its own directly managed worker process.
bool ReadGuardStartup(int fd, GuardStartupMessage* message) {
  auto* out = reinterpret_cast<char*>(message);
  size_t received = 0;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kGuardStartupTimeoutMsec);
  while (received < sizeof(*message)) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) {
      return false;
    }
    pollfd descriptor = {};
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    int poll_result;
    do {
      poll_result = ::poll(&descriptor, 1, static_cast<int>(remaining));
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result <= 0) {
      return false;
    }
    const ssize_t n = ::read(fd, out + received, sizeof(*message) - received);
    if (n > 0) {
      received += static_cast<size_t>(n);
    } else if (n == 0) {
      return false;
    } else if (errno != EINTR) {
      return false;
    }
  }
  return message->ok == 1 && message->worker_pid > 0;
}

std::string SanitizeCapturedOutput(std::string output,
                                   std::string_view api_key) {
  if (!api_key.empty()) {
    size_t position = 0;
    while ((position = output.find(api_key, position)) != std::string::npos) {
      output.replace(position, api_key.size(), "<redacted>");
      position += sizeof("<redacted>") - 1;
    }
  }

  bool previous_space = false;
  size_t write_position = 0;
  for (const unsigned char character : output) {
    const bool is_space = character == ' ' || character == '\t' ||
                          character == '\r' || character == '\n';
    if (is_space) {
      if (!previous_space && write_position > 0) {
        output[write_position++] = ' ';
      }
      previous_space = true;
      continue;
    }
    previous_space = false;
    output[write_position++] =
        (character < 0x20 || character == 0x7F)
            ? '?'
            : static_cast<char>(character);
  }
  output.resize(write_position);
  while (!output.empty() && output.back() == ' ') {
    output.pop_back();
  }
  return output;
}

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
  std::string guard_error;
  if (!ValidateExecutable(options.guard_executable_path, &guard_error)) {
    *error = "guard_" + guard_error;
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
    // Additional arguments follow the fixed loopback host / random port.
    // Never allow an argument to override this network isolation boundary.
    if (argument == "--host" || argument.starts_with("--host=") ||
        argument == "--port" || argument.starts_with("--port=")) {
      *error = "network_endpoint_override_rejected";
      return false;
    }
    // API key is controlled by Mozkey; never accept a second key in argv.
    if (argument == "--api-key" ||
        argument.starts_with("--api-key=")) {
      *error = "api_key_override_rejected";
      return false;
    }
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
  arguments.reserve(16 + options.additional_args.size());
  arguments.push_back(options.executable_path);
  arguments.push_back("-m");
  arguments.push_back(options.model_path);
  arguments.push_back("-c");
  arguments.push_back(std::to_string(options.context_size));
  arguments.push_back("-t");
  arguments.push_back(std::to_string(options.threads));
  if (options.disable_global_prompt_cache) {
    arguments.push_back("--cache-ram");
    arguments.push_back("0");
  }
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

  if (pid_ > 0 && running()) {
    *error = "already_started";
    return false;
  }
  if (pid_ > 0 || output_thread_.joinable()) {
    Stop();
  }
  ClearCapturedOutput();

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

  const std::string api_key_for_redaction = api_key_;
  if (!WaitUntilReady(error)) {
    Stop();
    AppendCapturedOutput(api_key_for_redaction, error);
    return false;
  }
  return true;
}

bool LlamaServerProcess::Spawn(std::string* error) {
  std::vector<std::string> arguments =
      BuildArguments(options_, port_, api_key_);
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 3);
  argv.push_back(const_cast<char*>(options_.guard_executable_path.c_str()));
  argv.push_back(const_cast<char*>("--"));
  // Keep the raw token out of the guard's argv.  Only llama-server
  // receives it in its argv, after the guard reads the private socket.
  for (size_t i = 0; i < arguments.size(); ++i) {
    if (i > 0 && arguments[i - 1] == "--api-key") {
      argv.push_back(const_cast<char*>(kGuardApiKeyPlaceholder));
    } else {
      argv.push_back(arguments[i].data());
    }
  }
  argv.push_back(nullptr);

  int output_pipe[2] = {-1, -1};
  int lifetime_pipe[2] = {-1, -1};
  int startup_pipe[2] = {-1, -1};
  int key_channel[2] = {-1, -1};
  if (!CreateOutputPipe(output_pipe, error)) {
    return false;
  }
  if (::pipe(lifetime_pipe) != 0 || ::pipe(startup_pipe) != 0 ||
      ::socketpair(AF_UNIX, SOCK_STREAM, 0, key_channel) != 0 ||
      !MoveFdToGuardSource(&output_pipe[0]) ||
      !MoveFdToGuardSource(&output_pipe[1]) ||
      !MoveFdToGuardSource(&lifetime_pipe[0]) ||
      !MoveFdToGuardSource(&lifetime_pipe[1]) ||
      !MoveFdToGuardSource(&startup_pipe[0]) ||
      !MoveFdToGuardSource(&startup_pipe[1]) ||
      !MoveFdToGuardSource(&key_channel[0]) ||
      !MoveFdToGuardSource(&key_channel[1])) {
    ClosePipe(output_pipe);
    ClosePipe(lifetime_pipe);
    ClosePipe(startup_pipe);
    ClosePipe(key_channel);
    *error = "guard_pipe_setup_failed";
    return false;
  }

  // macOS SO_NOSIGPIPE prevents a dead guard from terminating the scorer
  // while transferring a 64-byte key. No global signal handler is changed.
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  if (::setsockopt(key_channel[0], SOL_SOCKET, SO_NOSIGPIPE,
                   &enabled, sizeof(enabled)) != 0) {
    ClosePipe(output_pipe);
    ClosePipe(lifetime_pipe);
    ClosePipe(startup_pipe);
    ClosePipe(key_channel);
    *error = "guard_key_channel_nosigpipe_failed";
    return false;
  }
#else
  ClosePipe(output_pipe);
  ClosePipe(lifetime_pipe);
  ClosePipe(startup_pipe);
  ClosePipe(key_channel);
  *error = "guard_key_channel_nosigpipe_unavailable";
  return false;
#endif

  posix_spawn_file_actions_t actions;
  int result = ::posix_spawn_file_actions_init(&actions);
  if (result != 0) {
    ClosePipe(output_pipe);
    ClosePipe(lifetime_pipe);
    ClosePipe(startup_pipe);
    ClosePipe(key_channel);
    *error = "guard_file_actions_init_failed_" + std::to_string(result);
    return false;
  }
  result = ::posix_spawn_file_actions_adddup2(
      &actions, output_pipe[1], STDOUT_FILENO);
  if (result == 0) {
    result = ::posix_spawn_file_actions_adddup2(
        &actions, output_pipe[1], STDERR_FILENO);
  }
  if (result == 0) {
    result = ::posix_spawn_file_actions_adddup2(
        &actions, lifetime_pipe[0], kGuardLifetimeFd);
  }
  if (result == 0) {
    result = ::posix_spawn_file_actions_adddup2(
        &actions, startup_pipe[1], kGuardStartupFd);
  }
  if (result == 0) {
    result = ::posix_spawn_file_actions_adddup2(
        &actions, key_channel[1], kGuardApiKeyFd);
  }
  // Explicitly remove unneeded pipe ends in the guard. The worker will
  // separately close kGuardLifetimeFd and kGuardStartupFd before exec.
  for (int fd : {output_pipe[0], output_pipe[1], lifetime_pipe[0],
                 lifetime_pipe[1], startup_pipe[0], startup_pipe[1],
                 key_channel[0], key_channel[1]}) {
    if (result == 0) {
      result = ::posix_spawn_file_actions_addclose(&actions, fd);
    }
  }
  posix_spawnattr_t attributes;
  bool attr_initialized = false;
  if (result == 0) {
    result = ::posix_spawnattr_init(&attributes);
    attr_initialized = result == 0;
  }
  if (result == 0) {
    short flags = POSIX_SPAWN_SETPGROUP;
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    result = ::posix_spawnattr_setflags(&attributes, flags);
  }
  if (result == 0) {
    result = ::posix_spawnattr_setpgroup(&attributes, 0);
  }
  pid_t guard_pid = -1;
  if (result == 0) {
    result = ::posix_spawn(&guard_pid, options_.guard_executable_path.c_str(),
                           &actions, &attributes, argv.data(), *_NSGetEnviron());
  }
  if (attr_initialized) {
    ::posix_spawnattr_destroy(&attributes);
  }
  ::posix_spawn_file_actions_destroy(&actions);
  ::close(output_pipe[1]);
  output_pipe[1] = -1;
  ::close(lifetime_pipe[0]);
  lifetime_pipe[0] = -1;
  ::close(startup_pipe[1]);
  startup_pipe[1] = -1;
  ::close(key_channel[1]);
  key_channel[1] = -1;
  if (result != 0 || guard_pid <= 0) {
    ClosePipe(output_pipe);
    ClosePipe(lifetime_pipe);
    ClosePipe(startup_pipe);
    ClosePipe(key_channel);
    *error = "guard_spawn_failed_" + std::to_string(result);
    return false;
  }

  pid_ = guard_pid;
  lifetime_write_fd_ = lifetime_pipe[1];
  lifetime_pipe[1] = -1;
  bool key_sent = true;
  size_t key_bytes = 0;
  while (key_bytes < api_key_.size()) {
    const ssize_t sent = ::send(key_channel[0],
        api_key_.data() + key_bytes, api_key_.size() - key_bytes, 0);
    if (sent > 0) {
      key_bytes += static_cast<size_t>(sent);
    } else if (sent < 0 && errno == EINTR) {
      continue;
    } else {
      key_sent = false;
      break;
    }
  }
  ClosePipe(key_channel);
  GuardStartupMessage message;
  const bool started = key_sent &&
      ReadGuardStartup(startup_pipe[0], &message);
  ClosePipe(startup_pipe);
  if (!started) {
    // Closing the lifetime pipe initiates the guard's normal child cleanup.
    Stop();
    ClosePipe(output_pipe);
    *error = "guard_worker_start_failed";
    return false;
  }
  worker_pid_ = message.worker_pid;
  try {
    output_thread_ =
        std::thread(&LlamaServerProcess::CaptureOutput, this, output_pipe[0]);
  } catch (...) {
    ClosePipe(output_pipe);
    Stop();
    *error = "output_capture_thread_failed";
    return false;
  }
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
    // Readiness only needs to prove that authenticated inference can finish.
    // Use the minimum supported generation size so cold-start validation does
    // not spend time producing tokens that will be discarded.
    request.n_predict = 4;
    request.timeout_msec = static_cast<uint32_t>(std::min(
        options_.readiness_probe_timeout_msec, remaining_msec));
    request.max_output_chars = 4;
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
  // Never send a signal to the worker from the scorer. The guard owns the
  // worker's PID and process group, and observes lifetime EOF even on SIGKILL.
  if (lifetime_write_fd_ >= 0) {
    ::close(lifetime_write_fd_);
    lifetime_write_fd_ = -1;
  }
  if (pid_ <= 0) {
    JoinOutputCapture();
    ResetState();
    return;
  }
  const pid_t guard_pid = pid_;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kGuardShutdownTimeoutMsec);
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    if (ReapIfExited(&status)) {
      JoinOutputCapture();
      ResetState();
      return;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kStopPollIntervalMsec));
  }
  // Emergency fallback for an unresponsive guard. This cannot guarantee
  // worker cleanup if the guard is fatally broken; it is not used on the
  // expected graceful or scorer-SIGKILL paths.
  ::kill(guard_pid, SIGTERM);
  const auto term_deadline = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(1000);
  while (std::chrono::steady_clock::now() < term_deadline) {
    int status = 0;
    if (ReapIfExited(&status)) {
      JoinOutputCapture();
      ResetState();
      return;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kStopPollIntervalMsec));
  }
  ::kill(guard_pid, SIGKILL);
  int status = 0;
  pid_t result;
  do {
    result = ::waitpid(guard_pid, &status, 0);
  } while (result < 0 && errno == EINTR);
  pid_ = -1;
  JoinOutputCapture();
  ResetState();
}

void LlamaServerProcess::CaptureOutput(int fd) {
  std::array<char, 1024> buffer = {};
  while (true) {
    // Even if a guard unexpectedly dies while its worker survives, Join must
    // never hang on an inherited stdout/stderr pipe that still has a writer.
    pollfd descriptor = {};
    descriptor.fd = fd;
    descriptor.events = POLLIN | POLLHUP;
    int ready;
    do {
      ready = ::poll(&descriptor, 1,
                     stop_output_capture_.load() ? 0 : 100);
    } while (ready < 0 && errno == EINTR &&
             !stop_output_capture_.load());
    if (ready < 0) {
      break;
    }
    if (ready == 0) {
      if (stop_output_capture_.load()) {
        break;
      }
      continue;
    }
    const ssize_t result = ::read(fd, buffer.data(), buffer.size());
    if (result > 0) {
      const size_t bytes = static_cast<size_t>(result);
      std::lock_guard<std::mutex> lock(output_mutex_);
      if (bytes >= kMaximumCapturedOutputBytes) {
        output_tail_.assign(buffer.data() + bytes - kMaximumCapturedOutputBytes,
                            kMaximumCapturedOutputBytes);
      } else {
        output_tail_.append(buffer.data(), bytes);
        if (output_tail_.size() > kMaximumCapturedOutputBytes) {
          output_tail_.erase(
              0, output_tail_.size() - kMaximumCapturedOutputBytes);
        }
      }
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  ::close(fd);
}

void LlamaServerProcess::JoinOutputCapture() {
  stop_output_capture_.store(true);
  if (output_thread_.joinable()) {
    output_thread_.join();
  }
}

void LlamaServerProcess::ClearCapturedOutput() {
  stop_output_capture_.store(false);
  std::lock_guard<std::mutex> lock(output_mutex_);
  output_tail_.clear();
}

void LlamaServerProcess::AppendCapturedOutput(std::string_view api_key,
                                              std::string* error) {
  std::string output;
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output = output_tail_;
  }
  output = SanitizeCapturedOutput(std::move(output), api_key);
  if (output.empty()) {
    return;
  }
  error->append(":llama_server_output=");
  error->append(output);
}

void LlamaServerProcess::ResetState() {
  pid_ = -1;
  worker_pid_ = -1;
  if (lifetime_write_fd_ >= 0) {
    ::close(lifetime_write_fd_);
    lifetime_write_fd_ = -1;
  }
  port_ = 0;
  std::fill(api_key_.begin(), api_key_.end(), '\0');
  api_key_.clear();
}

}  // namespace zenz_scorer
}  // namespace mozc
