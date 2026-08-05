#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <limits.h>
#include <mach-o/dyld.h>

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

mozc::zenz_scorer::PosixScorerRuntimeOptions LoadRuntimeOptions() {
  const std::string executable_directory = DirectoryName(GetExecutablePath());

  mozc::zenz_scorer::PosixScorerRuntimeOptions options;
  options.socket_path = mozc::zenz::GetZenzUnixSocketPath();
  options.llama_server.executable_path =
      JoinPath(executable_directory, "llama-server");
  options.llama_server.model_path =
      JoinPath(JoinPath(executable_directory, "models"), kModelFileName);
  options.llama_server.context_size = kDefaultContextSize;
  options.llama_server.threads = kDefaultThreads;
  options.n_predict = kDefaultNPredict;
  return options;
}

}  // namespace

int main() {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  mozc::zenz_scorer::PosixScorerRuntime runtime(LoadRuntimeOptions());
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
