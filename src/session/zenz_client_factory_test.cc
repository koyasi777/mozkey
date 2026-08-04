#include "session/zenz_client_factory.h"

#include <memory>

#include "session/zenz_live_corrector.h"
#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzClientFactoryTest, CreatesCurrentPlatformClient) {
  std::unique_ptr<ZenzClient> client = CreateZenzClient();
  ASSERT_NE(client, nullptr);

#if defined(_WIN32)
  EXPECT_TRUE(client->IsAvailable());
#else
  EXPECT_FALSE(client->IsAvailable());
#endif
}

}  // namespace
}  // namespace session
}  // namespace mozc
