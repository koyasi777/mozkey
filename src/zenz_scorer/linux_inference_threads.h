#ifndef MOZC_ZENZ_SCORER_LINUX_INFERENCE_THREADS_H_
#define MOZC_ZENZ_SCORER_LINUX_INFERENCE_THREADS_H_

#include <sched.h>

#include <algorithm>

namespace mozc::zenz_scorer {

// The configured default is a ceiling, not a minimum. An unknown or empty
// affinity mask must not cause oversubscription on a constrained host.
constexpr int SelectLinuxInferenceThreads(int preferred, int available) {
  return std::max(1, std::min(preferred, available));
}

// A child inherits the spawning thread's CPU affinity. Query that mask rather
// than the host's total logical CPU count. If it cannot be read (including a
// mask too large for cpu_set_t), conservatively use one inference thread.
inline int GetLinuxInferenceThreads(int preferred) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (::sched_getaffinity(0, sizeof(mask), &mask) != 0) {
    return 1;
  }
  return SelectLinuxInferenceThreads(preferred, CPU_COUNT(&mask));
}

}  // namespace mozc::zenz_scorer

#endif  // MOZC_ZENZ_SCORER_LINUX_INFERENCE_THREADS_H_
