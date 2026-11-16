#include "renderer/win32/text_renderer.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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

TEST(TextRendererTest, VerticalRubyMeasurementProducesSingleColumn) {
  std::unique_ptr<TextRenderer> renderer = TextRenderer::Create(kTestDpi);
  ASSERT_NE(renderer, nullptr);

  if (!renderer->SupportsVerticalText(TextRenderer::FONTSET_RUBY)) {
    GTEST_SKIP() << "DirectWrite vertical ruby text is unavailable.";
  }

  const Size size = renderer->MeasureStringVertical(
      TextRenderer::FONTSET_RUBY, L"\u3042\u3044\u3059\u308b");

  EXPECT_GT(size.width, 0);
  EXPECT_GT(size.height, 0);
  EXPECT_GT(size.height, size.width);
}

// Render the same long vertical prefix with two distinct final glyphs.
// Only the newest (leftmost) columns should remain inside the viewport.
TEST(TextRendererTest, VerticalWrappedViewportDisplaysLatestCharacter) {
  std::unique_ptr<TextRenderer> renderer = TextRenderer::Create(kTestDpi);
  ASSERT_NE(renderer, nullptr);
  ASSERT_TRUE(renderer->SupportsVerticalWrappedText(
      TextRenderer::FONTSET_RUBY))
      << "This regression requires native DirectWrite vertical wrapping.";

  constexpr int kBitmapWidth = 120;
  constexpr int kBitmapHeight = 100;
  constexpr int kColumnHeight = 70;
  constexpr int kVisibleWidth = 64;
  const std::wstring common(160, L'\u65E5');
  const std::wstring reading_a = common + L'\u6728';  // 木
  const std::wstring reading_b = common + L'\u76EE';  // 目
  const Size a_layout = renderer->MeasureStringVerticalWrapped(
      TextRenderer::FONTSET_RUBY, reading_a, kColumnHeight);
  const Size b_layout = renderer->MeasureStringVerticalWrapped(
      TextRenderer::FONTSET_RUBY, reading_b, kColumnHeight);
  ASSERT_GT(a_layout.width, kVisibleWidth);
  ASSERT_GT(b_layout.width, kVisibleWidth);
  ASSERT_EQ(a_layout.width, b_layout.width)
      << "Same-length full-width suffix glyphs should have equal layout width.";

  const auto render = [&](const std::wstring &text, int full_width) {
    std::vector<uint8_t> output;
    HDC dc = ::CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
      return output;
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kBitmapWidth;
    info.bmiHeader.biHeight = -kBitmapHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(
        dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap != nullptr && bits != nullptr) {
      HGDIOBJ previous = ::SelectObject(dc, bitmap);
      if (previous != nullptr && previous != HGDI_ERROR) {
        std::memset(bits, 0xff, kBitmapWidth * kBitmapHeight * 4);
        renderer->RenderTextVerticalWrappedViewport(
            dc, text, Rect(8, 8, kVisibleWidth, kColumnHeight),
            full_width, TextRenderer::FONTSET_RUBY);
        ::GdiFlush();
        const auto *bytes = static_cast<const uint8_t *>(bits);
        output.assign(bytes, bytes + kBitmapWidth * kBitmapHeight * 4);
        ::SelectObject(dc, previous);
      }
      ::DeleteObject(bitmap);
    }
    ::DeleteDC(dc);
    return output;
  };

  const std::vector<uint8_t> image_a = render(reading_a, a_layout.width);
  const std::vector<uint8_t> image_b = render(reading_b, b_layout.width);
  ASSERT_FALSE(image_a.empty());
  ASSERT_FALSE(image_b.empty());
  EXPECT_TRUE(image_a != image_b)
      << "Different final characters must alter visible vertical pixels.";
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
