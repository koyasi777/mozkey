#ifndef MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_
#define MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_

#include <memory>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <sys/types.h>
#endif  // TARGET_OS_OSX
#endif  // __APPLE__

namespace mozc {
namespace session {

class ZenzClient;

// Creates the platform transport used by ZenzLiveCorrector.
//
// This factory keeps Session independent from concrete IPC transports.  The
// Windows uses the existing named-pipe client. macOS uses a Unix-domain
// socket client with a scorer launcher. Desktop Linux uses an XDG-runtime
// Unix-domain socket client with a fixed-path, persistent-thread scorer
// launcher. Other platforms retain the unavailable named-pipe fallback.
std::unique_ptr<ZenzClient> CreateZenzClient();

#if defined(__linux__) && !defined(__ANDROID__)
namespace internal {
// Test-only explicit path seam. Production never calls this entry point and
// never accepts an executable path supplied by a caller or environment var.
std::unique_ptr<ZenzClient> CreateLinuxZenzClientForTest(
    std::string socket_path, std::string launcher_path);
}  // namespace internal
#endif  // __linux__ && !__ANDROID__

#if defined(__APPLE__) && TARGET_OS_OSX
namespace internal {
// Starts the asynchronous waitpid() reaper for a child owned by this process.
// Exposed for the macOS process-lifecycle regression tests; callers must pass
// only a positive PID returned by a successful child spawn.
void ReapScorerAsync(pid_t pid);
}  // namespace internal
#endif  // __APPLE__ && TARGET_OS_OSX

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CLIENT_FACTORY_H_
