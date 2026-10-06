// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "win32/base/text_icon.h"

#include <safeint.h>
#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "base/win32/wide_char.h"

namespace mozc {
namespace win32 {
namespace {

using ::msl::utilities::SafeCast;
using ::msl::utilities::SafeMultiply;

RGBQUAD ToRGBQuad(DWORD color_ref) {
  const RGBQUAD rgbquad = {GetBValue(color_ref), GetGValue(color_ref),
                           GetRValue(color_ref), 0xff};
  return rgbquad;
}

void DrawPrivacyShield(HDC dc, int bitmap_width, int bitmap_height) {
  if (dc == nullptr || bitmap_width < 8 || bitmap_height < 8) {
    return;
  }

  const int shield_width =
      std::clamp(bitmap_width * 7 / 16, 6, bitmap_width);
  const int shield_height =
      std::clamp(bitmap_height * 8 / 16, 7, bitmap_height);
  const int left = bitmap_width - shield_width;
  const int top = bitmap_height - shield_height;
  const int right = bitmap_width - 1;
  const int bottom = bitmap_height - 1;
  const int center = (left + right) / 2;

  const int middle_y = top + shield_height / 2;
  POINT shield[] = {
      {left, top + 1},
      {center, top},
      {right, top + 1},
      {right, middle_y},
      {right - 1, bottom - 2},
      {center, bottom},
      {left + 1, bottom - 2},
      {left, middle_y},
  };

  HGDIOBJ old_brush = ::SelectObject(dc, ::GetStockObject(DC_BRUSH));
  HGDIOBJ old_pen = ::SelectObject(dc, ::GetStockObject(DC_PEN));
  ::SetDCBrushColor(dc, RGB(0xff, 0xff, 0xff));
  ::SetDCPenColor(dc, RGB(0xff, 0xff, 0xff));
  ::Polygon(dc, shield, 8);
  ::SelectObject(dc, old_pen);
  ::SelectObject(dc, old_brush);
}

HICON CreateMonochromeIconInternal(int bitmap_width, int bitmap_height,
                                   absl::string_view text,
                                   absl::string_view fontname,
                                   COLORREF text_color,
                                   bool draw_privacy_shield) {
  struct MonochromeBitmapInfo {
    BITMAPINFOHEADER header;
    RGBQUAD color_palette[2];
  };

  uint8_t* src_dib_buffer = nullptr;
  wil::unique_hbitmap src_dib;

  // Step 1. Create a src black-and-white DIB as follows.
  //  - This is a top-down DIB.
  //  - pixel bit is 0 if the pixel should be opaque for text image.
  //  - pixel bit is 1 if the pixel should be transparent.
  //  - |src_dib_buffer| is a 4-byte aligned bitmap image.
  {
    constexpr DWORD kBackgroundColor = RGB(0x00, 0x00, 0x00);
    constexpr DWORD kForegroundColor = RGB(0xff, 0xff, 0xff);

    MonochromeBitmapInfo info = {};
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = bitmap_width;
    info.header.biHeight = -bitmap_height;  // negavive value for top-down BMP
    info.header.biPlanes = 1;
    info.header.biBitCount = 1;
    info.header.biCompression = BI_RGB;
    info.header.biSizeImage = 0;
    // Note: these color is not directly used for the final output. All we need
    // to do here is to use different color and to make sure all the background
    // pixels are filled with 1.
    info.color_palette[0] = ToRGBQuad(kForegroundColor);
    info.color_palette[1] = ToRGBQuad(kBackgroundColor);

    src_dib.reset(::CreateDIBSection(
        nullptr, reinterpret_cast<const BITMAPINFO*>(&info), DIB_RGB_COLORS,
        reinterpret_cast<void**>(&src_dib_buffer), nullptr, 0));
    if (!src_dib.is_valid()) {
      return nullptr;
    }

    wil::unique_hdc dc(::CreateCompatibleDC(nullptr));
    if (!dc.is_valid()) {
      return nullptr;
    }

    wil::unique_select_object old_bitmap(
        wil::SelectObject(dc.get(), src_dib.get()));
    LOGFONT logfont = {};
    {
      logfont.lfWeight = FW_NORMAL;
      logfont.lfCharSet = DEFAULT_CHARSET;
      logfont.lfHeight = bitmap_height;
      logfont.lfQuality = NONANTIALIASED_QUALITY;
      const std::wstring wide_fontname = win32::Utf8ToWide(fontname);
      const errno_t error = wcscpy_s(logfont.lfFaceName, wide_fontname.c_str());
      if (error != 0) {
        return nullptr;
      }
    }

    wil::unique_hfont font_handle(::CreateFontIndirect(&logfont));
    if (!font_handle.is_valid()) {
      return nullptr;
    }

    wil::unique_select_object old_font(
        wil::SelectObject(dc.get(), font_handle.get()));
    ::SetBkMode(dc.get(), OPAQUE);
    ::SetBkColor(dc.get(), kBackgroundColor);
    ::SetTextColor(dc.get(), kForegroundColor);
    const std::wstring wide_text = win32::Utf8ToWide(text);
    RECT rect = {0, 0, bitmap_width, bitmap_height};
    ::ExtTextOutW(dc.get(), 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
    ::DrawTextW(dc.get(), wide_text.c_str(), wide_text.size(), &rect,
                DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CENTER);
    if (draw_privacy_shield) {
      DrawPrivacyShield(dc.get(), bitmap_width, bitmap_height);
    }
    ::GdiFlush();
  }

  BITMAP src_bmp_info = {};
  if (::GetObject(src_dib.get(), sizeof(src_bmp_info), &src_bmp_info) == 0) {
    return nullptr;
  }

  // Step 2. Create XOR bitmap.
  //  - This is a top-down DIB.
  //  - pixel bit is 0 if the pixel should be opaque for text image.
  //    - color palette for this pixel should be |text_color|.
  //  - pixel bit is 1 if the pixel should be transparent.
  //    - color palette for this pixel should be RGB(0, 0, 0), which has null
  //      effect when XOR operation is done.
  wil::unique_hbitmap xor_dib;
  {
    MonochromeBitmapInfo info = {};
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = bitmap_width;
    info.header.biHeight = -bitmap_height;  // negavive value for top-down BMP
    info.header.biPlanes = 1;
    info.header.biBitCount = 1;
    info.header.biCompression = BI_RGB;
    info.header.biSizeImage = 0;
    // Foreground pixel: should be initialized with the given |text_color|.
    info.color_palette[0] = ToRGBQuad(text_color);
    // Background pixel: should be 0, which has null effect in XOR operation.
    info.color_palette[1] = ToRGBQuad(0);

    uint8_t* xor_dib_buffer = nullptr;
    xor_dib.reset(::CreateDIBSection(
        nullptr, reinterpret_cast<const BITMAPINFO*>(&info), DIB_RGB_COLORS,
        reinterpret_cast<void**>(&xor_dib_buffer), nullptr, 0));
    if (!xor_dib.is_valid()) {
      return nullptr;
    }

    // Make sure that |xor_dib| and |src_dib| have the same pixel format.
    BITMAP xor_dib_info = {};
    if (!::GetObject(xor_dib.get(), sizeof(xor_dib_info), &xor_dib_info) ||
        (xor_dib_info.bmBitsPixel != src_bmp_info.bmBitsPixel) ||
        (xor_dib_info.bmWidthBytes != src_bmp_info.bmWidthBytes) ||
        (xor_dib_info.bmHeight != src_bmp_info.bmHeight)) {
      return nullptr;
    }

    // Here we should be able to copy the buffer safely.
    const size_t data_len = bitmap_height * src_bmp_info.bmWidthBytes;
    ::memcpy(xor_dib_buffer, src_dib_buffer, data_len);
  }

  // Step 3. Create AND bitmap.
  //  - This is a top-down DDB.
  //  - pixel bit is 0 if the pixel should be opaque for text image.
  //  - pixel bit is 1 if the pixel should be transparent.
  wil::unique_hbitmap mask_ddb;
  {
    // Note: each line should be aligned with 2-byte for DDB while DIB uses
    // 4-byte alignment. Here we need to do alignment conversion.
    const size_t mask_buffer_stride = (bitmap_width + 0x0f) / 16 * 2;
    const size_t mask_buffer_size = mask_buffer_stride * bitmap_width;
    if (src_dib_buffer == nullptr) {
      return nullptr;
    }
    auto mask_buffer = std::make_unique<uint8_t[]>(mask_buffer_size);
    for (size_t y = 0; y < bitmap_height; ++y) {
      for (size_t x = 0; x < bitmap_width; ++x) {
        const uint8_t* src_line_start =
            src_dib_buffer + src_bmp_info.bmWidthBytes * y;
        uint8_t* dest_line_start = mask_buffer.get() + mask_buffer_stride * y;
        ::memcpy(dest_line_start, src_line_start, mask_buffer_stride);
      }
    }
    mask_ddb.reset(
        ::CreateBitmap(bitmap_width, bitmap_height, 1, 1, mask_buffer.get()));
    if (!mask_ddb.is_valid()) {
      return nullptr;
    }
  }

  // Step 4. Create a GDI ICON object.
  {
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = xor_dib.get();
    info.hbmMask = mask_ddb.get();
    info.xHotspot = 0;
    info.yHotspot = 0;
    return ::CreateIconIndirect(&info);
  }
}

}  // namespace

// static
HICON TextIcon::CreateMonochromeIcon(size_t width, size_t height,
                                     absl::string_view text,
                                     absl::string_view fontname,
                                     COLORREF text_color) {
  int safe_width = 0;
  int safe_height = 0;
  int safe_num_pixels = 0;
  if (!SafeCast(width, safe_width) || !SafeCast(height, safe_height) ||
      !SafeMultiply(safe_width, safe_height, safe_num_pixels)) {
    LOG(ERROR) << "Requested size is too large." << " width: " << width
               << " height: " << height;
    return nullptr;
  }

  return CreateMonochromeIconInternal(safe_width, safe_height, text, fontname,
                                      text_color, false);
}

// static
HICON TextIcon::CreateMonochromeIconWithPrivacyShield(
    size_t width, size_t height, absl::string_view text,
    absl::string_view fontname, COLORREF text_color) {
  int safe_width = 0;
  int safe_height = 0;
  int safe_num_pixels = 0;
  if (!SafeCast(width, safe_width) || !SafeCast(height, safe_height) ||
      !SafeMultiply(safe_width, safe_height, safe_num_pixels)) {
    LOG(ERROR) << "Requested size is too large." << " width: " << width
               << " height: " << height;
    return nullptr;
  }

  return CreateMonochromeIconInternal(safe_width, safe_height, text, fontname,
                                      text_color, true);
}

namespace {

HICON CreateAdaptiveIconInternal(
    size_t width, size_t height, absl::string_view text,
    absl::string_view fontname, COLORREF text_color,
    bool draw_privacy_shield) {
  int safe_width = 0;
  int safe_height = 0;
  if (!SafeCast(width, safe_width) || !SafeCast(height, safe_height)) {
    LOG(ERROR) << "Requested size is too large." << " width: " << width
               << " height: " << height;
    return nullptr;
  }

  // First reuse the existing 1-bit renderer to obtain the exact text and
  // optional shield AND mask. We then materialize that mask as a 32-bit alpha
  // image. This
  // matches the structural class of the normal *_a.ico resources used by the
  // Windows 8+ taskbar mode item (hbmColor present + single-height hbmMask).
  wil::unique_hicon rendered(
      draw_privacy_shield
          ? TextIcon::CreateMonochromeIconWithPrivacyShield(
                width, height, text, fontname, RGB(0, 0, 0))
          : TextIcon::CreateMonochromeIcon(
                width, height, text, fontname, RGB(0, 0, 0)));
  if (!rendered.is_valid()) {
    return nullptr;
  }

  ICONINFO rendered_info = {};
  if (!::GetIconInfo(rendered.get(), &rendered_info)) {
    return nullptr;
  }
  wil::unique_hbitmap rendered_color(rendered_info.hbmColor);
  wil::unique_hbitmap rendered_mask(rendered_info.hbmMask);
  if (!rendered_mask.is_valid()) {
    return nullptr;
  }

  BITMAP mask_info = {};
  if (::GetObject(rendered_mask.get(), sizeof(mask_info), &mask_info) == 0 ||
      mask_info.bmBitsPixel != 1 || mask_info.bmWidth != safe_width ||
      mask_info.bmHeight != safe_height || mask_info.bmWidthBytes <= 0) {
    return nullptr;
  }

  struct MonochromeBitmapInfo {
    BITMAPINFOHEADER header;
    RGBQUAD palette[2];
  };

  const size_t dib_stride =
      (static_cast<size_t>(safe_width) + 31) / 32 * 4;
  size_t dib_size = 0;
  if (!SafeMultiply(dib_stride, static_cast<size_t>(safe_height), dib_size)) {
    return nullptr;
  }

  std::vector<uint8_t> top_down_mask(dib_size, 0);
  MonochromeBitmapInfo mask_dib_info = {};
  mask_dib_info.header.biSize = sizeof(mask_dib_info.header);
  mask_dib_info.header.biWidth = safe_width;
  mask_dib_info.header.biHeight = -safe_height;
  mask_dib_info.header.biPlanes = 1;
  mask_dib_info.header.biBitCount = 1;
  mask_dib_info.header.biCompression = BI_RGB;
  mask_dib_info.palette[0] = ToRGBQuad(RGB(0, 0, 0));
  mask_dib_info.palette[1] = ToRGBQuad(RGB(0xff, 0xff, 0xff));

  wil::unique_hdc dc(::CreateCompatibleDC(nullptr));
  if (!dc.is_valid()) {
    return nullptr;
  }

  const int copied_scanlines = ::GetDIBits(
      dc.get(), rendered_mask.get(), 0, safe_height, top_down_mask.data(),
      reinterpret_cast<BITMAPINFO*>(&mask_dib_info), DIB_RGB_COLORS);
  if (copied_scanlines != safe_height) {
    return nullptr;
  }

  BITMAPINFO color_dib_info = {};
  color_dib_info.bmiHeader.biSize = sizeof(color_dib_info.bmiHeader);
  color_dib_info.bmiHeader.biWidth = safe_width;
  color_dib_info.bmiHeader.biHeight = -safe_height;
  color_dib_info.bmiHeader.biPlanes = 1;
  color_dib_info.bmiHeader.biBitCount = 32;
  color_dib_info.bmiHeader.biCompression = BI_RGB;

  uint8_t* color_buffer = nullptr;
  wil::unique_hbitmap adaptive_color(::CreateDIBSection(
      nullptr, &color_dib_info, DIB_RGB_COLORS,
      reinterpret_cast<void**>(&color_buffer), nullptr, 0));
  if (!adaptive_color.is_valid() || color_buffer == nullptr) {
    return nullptr;
  }

  // The adaptive Windows taskbar resources use an alpha-bearing color icon.
  // Materialize the caller-selected foreground color as opaque pixels and
  // keep background pixels fully transparent.  The caller chooses the
  // foreground from the current Windows taskbar theme.
  for (int y = 0; y < safe_height; ++y) {
    const uint8_t* mask_row =
        top_down_mask.data() + dib_stride * static_cast<size_t>(y);
    for (int x = 0; x < safe_width; ++x) {
      const uint8_t bit =
          static_cast<uint8_t>(0x80u >> static_cast<unsigned>(x & 7));
      const bool transparent =
          (mask_row[static_cast<size_t>(x) / 8] & bit) != 0;

      uint8_t* pixel =
          color_buffer +
          (static_cast<size_t>(y) * static_cast<size_t>(safe_width) +
           static_cast<size_t>(x)) *
              4;

      if (!transparent) {
        pixel[0] = GetBValue(text_color);
        pixel[1] = GetGValue(text_color);
        pixel[2] = GetRValue(text_color);
        pixel[3] = 0xff;
      }
    }
  }

  size_t mask_size = 0;
  if (!SafeMultiply(static_cast<size_t>(mask_info.bmWidthBytes),
                    static_cast<size_t>(safe_height), mask_size)) {
    return nullptr;
  }

  LONG mask_size_long = 0;
  if (!SafeCast(mask_size, mask_size_long)) {
    return nullptr;
  }

  std::vector<uint8_t> mask_bits(mask_size, 0);
  if (::GetBitmapBits(rendered_mask.get(), mask_size_long, mask_bits.data()) !=
      mask_size_long) {
    return nullptr;
  }

  wil::unique_hbitmap adaptive_mask(
      ::CreateBitmap(safe_width, safe_height, 1, 1, mask_bits.data()));
  if (!adaptive_mask.is_valid()) {
    return nullptr;
  }

  ICONINFO output_info = {};
  output_info.fIcon = TRUE;
  output_info.hbmColor = adaptive_color.get();
  output_info.hbmMask = adaptive_mask.get();
  return ::CreateIconIndirect(&output_info);
}
}  // namespace

// static
HICON TextIcon::CreateAdaptiveIcon(size_t width, size_t height,
                                   absl::string_view text,
                                   absl::string_view fontname,
                                   COLORREF text_color) {
  return CreateAdaptiveIconInternal(width, height, text, fontname, text_color,
                                    false);
}

// static
HICON TextIcon::CreateAdaptiveIconWithPrivacyShield(
    size_t width, size_t height, absl::string_view text,
    absl::string_view fontname, COLORREF text_color) {
  return CreateAdaptiveIconInternal(width, height, text, fontname, text_color,
                                    true);
}
}  // namespace win32
}  // namespace mozc
