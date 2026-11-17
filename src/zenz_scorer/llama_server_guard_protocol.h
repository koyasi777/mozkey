#ifndef MOZC_ZENZ_SCORER_LLAMA_SERVER_GUARD_PROTOCOL_H_
#define MOZC_ZENZ_SCORER_LLAMA_SERVER_GUARD_PROTOCOL_H_

#include <cstdint>

namespace mozc::zenz_scorer {
// Local anonymous pipe protocol, private to binaries built from the same tree.
// Small enough for one atomic pipe write on macOS.
struct GuardStartupMessage {
  std::int32_t ok = 0;
  std::int32_t worker_pid = -1;
};
inline constexpr int kGuardLifetimeFd = 20;
inline constexpr int kGuardStartupFd = 21;
inline constexpr int kGuardApiKeyFd = 22;
// Worker-only read FDs.  Its two writers must live in different processes.
// The scorer owns the writer associated with kGuardLifetimeFd, while the
// guard owns a newly created second writer.  No third process owns either.
inline constexpr int kWorkerScorerLifetimeFd = 23;
inline constexpr int kWorkerGuardLifetimeFd = 24;
// Linux-only sealed anonymous file descriptor containing the ephemeral key.
// Worker reads it through /proc/self/fd/25; the key is never in worker argv.
inline constexpr int kWorkerApiKeyFd = 25;
inline constexpr char kWorkerDualLifetimeSwitch[] =
    "--mozkey-zenz-dual-liveness-v1";
// Placeholder is non-secret; the actual per-launch key travels via an
// inherited Unix socket, never in the guard's process arguments.
inline constexpr char kGuardApiKeyPlaceholder[] =
    "__MOZKEY_GUARD_KEY_FROM_FD__";
inline constexpr int kGuardApiKeyLength = 64;
}  // namespace mozc::zenz_scorer

#endif  // MOZC_ZENZ_SCORER_LLAMA_SERVER_GUARD_PROTOCOL_H_
