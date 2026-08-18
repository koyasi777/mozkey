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

TEST(TextRendererTest, VerticalWrappedMeasurementFlowsIntoLeftColumns) {
  std::unique_ptr<TextRenderer> renderer = TextRenderer::Create(kTestDpi);
  ASSERT_NE(renderer, nullptr);

  if (!renderer->SupportsVerticalWrappedText(
          TextRenderer::FONTSET_INFOLIST_DESCRIPTION)) {
    GTEST_SKIP() << "DirectWrite wrapped vertical text is unavailable.";
  }

  const Size one = renderer->MeasureStringVertical(
      TextRenderer::FONTSET_INFOLIST_DESCRIPTION, L"\u65E5");
  ASSERT_GT(one.width, 0);
  ASSERT_GT(one.height, 0);

  const int constrained_height = one.height * 4;
  const Size wrapped = renderer->MeasureStringVerticalWrapped(
      TextRenderer::FONTSET_INFOLIST_DESCRIPTION,
      L"\u65E5\u672C\u8A9E\u306E\u7528\u4F8B\u3092"
      L"\u7E26\u66F8\u304D\u3067\u8868\u793A\u3059\u308B\u3002",
      constrained_height);

  EXPECT_GT(wrapped.width, one.width);
  EXPECT_GT(wrapped.height, 0);
  EXPECT_LE(wrapped.height, constrained_height);
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
