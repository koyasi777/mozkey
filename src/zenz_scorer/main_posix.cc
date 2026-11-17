#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <unistd.h>
#else
#error "Zenz POSIX scorer requires macOS or desktop Linux"
#endif

#include "zenz/zenz_unix_socket_path.h"
#include "zenz_scorer/posix_runtime.h"

namespace {

constexpr int kDefaultContextSize = 256;
constexpr int kDefaultThreads = 4;
constexpr int kDefaultNPredict = 64;
constexpr char kModelFileName[] = "zenz-v3.2-small-Q5_K_M.gguf";

volatile std::sig_atomic_t g_shutdown_requested = 0;

void HandleSignal(int) { g_shutdown_requested = 1; }

std::string JoinPath(std::string_view directory, std::string_view name) {
  if (directory.empty()) {
    return std::string(name);
  }
  if (directory.back() == '/') {
    return std::string(directory) + std::string(name);
  }
  return std::string(directory) + "/" + std::string(name);
}

std::string GetExecutablePath() {
#if defined(__APPLE__)
  uint32_t size = PATH_MAX;
  std::vector<char> buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    buffer.assign(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
      return "";
    }
  }

  char resolved[PATH_MAX] = {};
  if (::realpath(buffer.data(), resolved) != nullptr) {
    return resolved;
  }
  return buffer.data();
#else
  // /proc/self/exe identifies the running binary, even if invoked via a
  // symlink. Grow the buffer rather than accepting a truncated result.
  for (size_t capacity = 256; capacity <= 65536; capacity *= 2) {
    std::vector<char> buffer(capacity);
    const ssize_t size = ::readlink("/proc/self/exe", buffer.data(),
                                    buffer.size());
    if (size <= 0) {
      return "";
    }
    if (static_cast<size_t>(size) < buffer.size()) {
      return std::string(buffer.data(), static_cast<size_t>(size));
    }
  }
  return "";
#endif
}

std::string DirectoryName(std::string_view path) {
  const size_t separator = path.find_last_of('/');
  if (separator == std::string_view::npos) {
    return ".";
  }
  if (separator == 0) {
    return "/";
  }
  return std::string(path.substr(0, separator));
}

mozc::zenz_scorer::PosixScorerRuntimeOptions LoadRuntimeOptions(
    std::string_view executable_directory, std::string_view socket_path) {
  mozc::zenz_scorer::PosixScorerRuntimeOptions options;
  options.socket_path = std::string(socket_path);
  options.llama_server.executable_path =
      JoinPath(executable_directory, "llama-server");
  options.llama_server.guard_executable_path =
      JoinPath(executable_directory, "mozc_zenz_guard");
  options.llama_server.model_path =
      JoinPath(JoinPath(executable_directory, "models"), kModelFileName);
  options.llama_server.context_size = kDefaultContextSize;
  options.llama_server.threads = kDefaultThreads;
  // Mozkey uses one local scorer client. Retaining historical prompt states in
  // llama-server's global RAM cache causes memory growth during continuous IME
  // use. Request-level cache_prompt remains enabled for current-prefix reuse.
  options.llama_server.disable_global_prompt_cache = true;
  options.n_predict = kDefaultNPredict;
  return options;
}

}  // namespace

int main() {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  const std::string executable_path = GetExecutablePath();
  if (executable_path.empty() || executable_path.front() != '/') {
    std::cerr << "[mozc-zenz-scorer] executable_path_unavailable\n";
    return 1;
  }
  const std::string socket_path = mozc::zenz::GetZenzUnixSocketPath();
  if (socket_path.empty()) {
    // Do not start the model or guard when the user runtime root is invalid.
    std::cerr << "[mozc-zenz-scorer] socket_path_unavailable\n";
    return 1;
  }

  mozc::zenz_scorer::PosixScorerRuntime runtime(
      LoadRuntimeOptions(DirectoryName(executable_path), socket_path));
  std::string error;
  if (!runtime.Start(&error)) {
    std::cerr << "[mozc-zenz-scorer] start failed: " << error << '\n';
    return 1;
  }

  while (g_shutdown_requested == 0) {
    if (!runtime.ServeOne(250, &error)) {
      std::cerr << "[mozc-zenz-scorer] serve failed: " << error << '\n';
      return 1;
    }
  }

  runtime.Stop();
  return 0;
}
