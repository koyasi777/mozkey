#include "renderer/win32/text_renderer.h"

#include <cstdint>
#include <memory>

#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

constexpr uint32_t kTestDpi = 96;

TEST(TextRendererTest, ExistingHorizontalMeasurementRemainsAvailable) {
  std::unique_ptr<TextRenderer> renderer = TextRenderer::Create(kTestDpi);
  ASSERT_NE(renderer, nullptr);

  const Size size = renderer->MeasureString(
      TextRenderer::FONTSET_CANDIDATE, L"\u65E5\u672C\u8A9E");
  EXPECT_GT(size.width, 0);
  EXPECT_GT(size.height, 0);
}

TEST(TextRendererTest, VerticalMeasurementProducesSingleTopToBottomColumn) {
  std::unique_ptr<TextRenderer> renderer = TextRenderer::Create(kTestDpi);
  ASSERT_NE(renderer, nullptr);

  if (!renderer->SupportsVerticalText(TextRenderer::FONTSET_CANDIDATE)) {
    GTEST_SKIP() << "DirectWrite vertical text is unavailable.";
  }

  const Size one = renderer->MeasureStringVertical(
      TextRenderer::FONTSET_CANDIDATE, L"\u65E5");
  const Size three = renderer->MeasureStringVertical(
      TextRenderer::FONTSET_CANDIDATE, L"\u65E5\u672C\u8A9E");

  EXPECT_GT(one.width, 0);
  EXPECT_GT(one.height, 0);
  EXPECT_GT(three.width, 0);
  EXPECT_GT(three.height, one.height);
  EXPECT_GE(three.height, three.width);
}

TEST(TextRendererTest, VerticalMeasurementHandlesPunctuationLatinAndDigits) {
  std::unique_ptr<TextRenderer> renderer = TextRenderer::Create(kTestDpi);
  ASSERT_NE(renderer, nullptr);

  if (!renderer->SupportsVerticalText(TextRenderer::FONTSET_CANDIDATE)) {
    GTEST_SKIP() << "DirectWrite vertical text is unavailable.";
  }

  const Size size = renderer->MeasureStringVertical(
      TextRenderer::FONTSET_CANDIDATE,
      L"\u300C\u65E5\u672C\u8A9E\u300D\u3001ABC2026\u3002");

  EXPECT_GT(size.width, 0);
  EXPECT_GT(size.height, 0);
  EXPECT_GT(size.height, size.width);
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
