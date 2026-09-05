#include "session/zenz_client_factory.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include "absl/log/log.h"
#include "base/file_util.h"
#include "base/process.h"
#include "base/system_util.h"
#endif  // TARGET_OS_OSX
#endif  // __APPLE__

#include "session/zenz_live_corrector.h"
#include "session/zenz_named_pipe_client.h"
#include "session/zenz_unix_socket_client.h"
#include "zenz/zenz_unix_socket_path.h"

namespace mozc {
namespace session {
namespace {

#if defined(__APPLE__) && TARGET_OS_OSX

constexpr char kZenzRuntimeDirectory[] = "ZenzRuntime";
constexpr char kZenzScorerExecutable[] = "mozc_zenz_scorer";
constexpr auto kScorerLaunchThrottle = std::chrono::seconds(2);

bool LaunchMacZenzScorer() {
  // MozcConverter is normally a singleton, but several Zenz worker requests
  // can observe the same cold start. Serialize the short spawn operation and
  // treat a recent successful launch as one already in progress.
  static std::mutex launch_mutex;
  static std::chrono::steady_clock::time_point last_launch;
  static bool has_launched = false;

  const std::lock_guard<std::mutex> lock(launch_mutex);
  const auto now = std::chrono::steady_clock::now();
  if (has_launched && now - last_launch < kScorerLaunchThrottle) {
    return true;
  }

  const std::string scorer_path = FileUtil::JoinPath({
      SystemUtil::GetServerDirectory(),
      kZenzRuntimeDirectory,
      kZenzScorerExecutable,
  });

  size_t pid = 0;
  if (!Process::SpawnProcess(scorer_path, "", &pid)) {
    LOG(ERROR) << "Failed to launch the bundled Zenz scorer";
    return false;
  }

  has_launched = true;
  last_launch = now;
  return true;
}

#endif  // __APPLE__ && TARGET_OS_OSX

}  // namespace

std::unique_ptr<ZenzClient> CreateZenzClient() {
#if defined(__APPLE__) && TARGET_OS_OSX
  return std::make_unique<ZenzUnixSocketClient>(
      ::mozc::zenz::GetZenzUnixSocketPath(), &LaunchMacZenzScorer);
#elif defined(__linux__)
  return std::make_unique<ZenzUnixSocketClient>(
      ::mozc::zenz::GetZenzUnixSocketPath());
#else
  return std::make_unique<ZenzNamedPipeClient>();
#endif  // __APPLE__ && TARGET_OS_OSX
}

}  // namespace session
}  // namespace mozc
