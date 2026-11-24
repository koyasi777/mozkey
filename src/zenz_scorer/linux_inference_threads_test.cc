#include "zenz_scorer/linux_inference_threads.h"

#include <sched.h>

#include "testing/gunit.h"

namespace mozc::zenz_scorer {
namespace {

TEST(LinuxInferenceThreadsTest, ClampsToAvailableCpuCount) {
  EXPECT_EQ(1, SelectLinuxInferenceThreads(4, 1));
  EXPECT_EQ(2, SelectLinuxInferenceThreads(4, 2));
  EXPECT_EQ(3, SelectLinuxInferenceThreads(4, 3));
  EXPECT_EQ(4, SelectLinuxInferenceThreads(4, 4));
  EXPECT_EQ(4, SelectLinuxInferenceThreads(4, 128));
}

TEST(LinuxInferenceThreadsTest, UsesConservativeMinimum) {
  EXPECT_EQ(1, SelectLinuxInferenceThreads(4, 0));
  EXPECT_EQ(1, SelectLinuxInferenceThreads(4, -1));
  EXPECT_EQ(1, SelectLinuxInferenceThreads(0, 8));
  EXPECT_EQ(1, SelectLinuxInferenceThreads(1, 8));
}

TEST(LinuxInferenceThreadsTest, UsesCurrentAffinity) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  const int result = ::sched_getaffinity(0, sizeof(mask), &mask);
  const int expected =
      result == 0 ? SelectLinuxInferenceThreads(4, CPU_COUNT(&mask)) : 1;
  EXPECT_EQ(expected, GetLinuxInferenceThreads(4));
}

}  // namespace
}  // namespace mozc::zenz_scorer
