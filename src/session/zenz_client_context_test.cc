#include "session/zenz_client_context.h"

#include "protocol/commands.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

TEST(ZenzClientContextTest, FallsBackToGenericContextWhenExtendedIsUnset) {
  commands::Context context;
  context.set_preceding_text("generic-left");
  context.set_following_text("generic-right");

  const ZenzClientContextView view = GetZenzClientContextView(context);

  EXPECT_EQ(view.preceding_text, "generic-left");
  EXPECT_EQ(view.following_text, "generic-right");
}

TEST(ZenzClientContextTest, ExtendedContextOverridesGenericPerSide) {
  commands::Context context;
  context.set_preceding_text("generic-left");
  context.set_following_text("generic-right");
  context.set_zenz_preceding_text("extended-left");
  context.set_zenz_following_text("extended-right");

  const ZenzClientContextView view = GetZenzClientContextView(context);

  EXPECT_EQ(view.preceding_text, "extended-left");
  EXPECT_EQ(view.following_text, "extended-right");
}

TEST(ZenzClientContextTest, ExtendedPresenceIsIndependentForEachDirection) {
  commands::Context context;
  context.set_preceding_text("generic-left");
  context.set_following_text("generic-right");
  context.set_zenz_preceding_text("extended-left");

  const ZenzClientContextView view = GetZenzClientContextView(context);

  EXPECT_EQ(view.preceding_text, "extended-left");
  EXPECT_EQ(view.following_text, "generic-right");
}

TEST(ZenzClientContextTest, ExplicitEmptyExtendedValueSuppressesFallback) {
  commands::Context context;
  context.set_preceding_text("generic-left");
  context.set_following_text("generic-right");
  context.set_zenz_preceding_text("");
  context.set_zenz_following_text("");

  ASSERT_TRUE(context.has_zenz_preceding_text());
  ASSERT_TRUE(context.has_zenz_following_text());

  const ZenzClientContextView view = GetZenzClientContextView(context);

  EXPECT_TRUE(view.preceding_text.empty());
  EXPECT_TRUE(view.following_text.empty());
}

TEST(ZenzClientContextTest, GenericContextIsNotMutated) {
  commands::Context context;
  context.set_preceding_text("generic-left");
  context.set_following_text("generic-right");
  context.set_zenz_preceding_text("extended-left");
  context.set_zenz_following_text("extended-right");

  const ZenzClientContextView view = GetZenzClientContextView(context);

  EXPECT_EQ(view.preceding_text, "extended-left");
  EXPECT_EQ(view.following_text, "extended-right");
  EXPECT_EQ(context.preceding_text(), "generic-left");
  EXPECT_EQ(context.following_text(), "generic-right");
}

}  // namespace
}  // namespace session
}  // namespace mozc
