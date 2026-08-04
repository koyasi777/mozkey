#include "session/zenz_client_factory.h"

#include <memory>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif  // __APPLE__

#include "session/zenz_live_corrector.h"
#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzClientFactoryTest, CreatesCurrentPlatformClient) {
  std::unique_ptr<ZenzClient> client = CreateZenzClient();
  ASSERT_NE(client, nullptr);

#if defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX)
  EXPECT_TRUE(client->IsAvailable());
#else
  EXPECT_FALSE(client->IsAvailable());
#endif
}

}  // namespace
}  // namespace session
}  // namespace mozc
