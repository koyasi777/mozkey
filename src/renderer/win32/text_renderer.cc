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

#include "renderer/win32/text_renderer.h"

#include <atlbase.h>
#include <atlcom.h>
#include <atltypes.h>
#include <d2d1.h>
#include <dwrite.h>
#include <objbase.h>
#include <wil/com.h>
#include <wil/resource.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "base/coordinates.h"
#include "base/win32/wide_char.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/win32/win32_dpi_util.h"
#include "renderer/win32/win32_font_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

using ::mozc::renderer::RendererStyle;

namespace {

  constexpr size_t kShortcutTextStyleIndex = 0;
  constexpr size_t kCandidateTextStyleIndex = 2;
  constexpr size_t kDescriptionTextStyleIndex = 3;

  CRect ToCRect(const Rect& rect) {
    return CRect(rect.Left(), rect.Top(), rect.Right(), rect.Bottom());
  }

  COLORREF ToColorRef(const RendererStyle::RGBAColor& color) {
    return RGB(static_cast<int>(color.r()), static_cast<int>(color.g()),
              static_cast<int>(color.b()));
  }

  COLORREF GetTextColor(TextRenderer::FONT_TYPE type, uint32_t dpi) {
    RendererStyle style;
    GetScaledRendererStyle(&style, dpi);

    switch (type) {
      case TextRenderer::FONTSET_SHORTCUT:
        if (style.shortcut_style().has_foreground_color()) {
          return ToColorRef(style.shortcut_style().foreground_color());
        }
        return RGB(0x61, 0x61, 0x61);

      case TextRenderer::FONTSET_CANDIDATE:
        if (style.candidate_style().has_foreground_color()) {
          return ToColorRef(style.candidate_style().foreground_color());
        }
        return RGB(0x00, 0x00, 0x00);

      case TextRenderer::FONTSET_DESCRIPTION:
        if (style.description_style().has_foreground_color()) {
          return ToColorRef(style.description_style().foreground_color());
        }
        return RGB(0x88, 0x88, 0x88);

      case TextRenderer::FONTSET_FOOTER_INDEX:
      case TextRenderer::FONTSET_FOOTER_LABEL:
        return ToColorRef(style.footer_style().foreground_color());

      case TextRenderer::FONTSET_FOOTER_SUBLABEL:
        return ToColorRef(style.footer_sub_label_style().foreground_color());

      case TextRenderer::FONTSET_INFOLIST_CAPTION:
        return ToColorRef(
            style.infolist_style().caption_style().foreground_color());

      case TextRenderer::FONTSET_INFOLIST_TITLE:
        return ToColorRef(style.infolist_style().title_style().foreground_color());

      case TextRenderer::FONTSET_INFOLIST_DESCRIPTION:
        return ToColorRef(
            style.infolist_style().description_style().foreground_color());

      default:
        LOG(DFATAL) << "Unknown type: " << type;
        return RGB(0, 0, 0);
    }
  }

  const RendererStyle::TextStyle* GetTextStyleOrNull(
      const RendererStyle& style, size_t index) {
    switch (index) {
      case kShortcutTextStyleIndex:
        return &style.shortcut_style();
      case kCandidateTextStyleIndex:
        return &style.candidate_style();
      case kDescriptionTextStyleIndex:
        return &style.description_style();
      default:
        return nullptr;
    }
  }

  bool IsSafeLogFontFaceName(const std::wstring& font_name) {
    if (font_name.empty()) {
      return false;
    }

    // Skip vertical font aliases such as "@Yu Gothic".
    if (font_name.front() == L'@') {
      return false;
    }

    // LOGFONTW::lfFaceName must include the terminating NUL.
    // Do not silently truncate; a truncated family name may resolve to a
    // different or invalid font in GDI/DirectWrite.
    if (font_name.size() >= LF_FACESIZE) {
      return false;
    }

    return true;
  }

  LOGFONTW GetLogFontWithDefaultFaceName(LOGFONTW log_font, uint32_t dpi) {
    const LOGFONTW default_font = GetMessageBoxLogFont(dpi);

    if (default_font.lfFaceName[0] != L'\0') {
      wcscpy_s(log_font.lfFaceName, default_font.lfFaceName);
    } else {
      wcscpy_s(log_font.lfFaceName, L"Yu Gothic UI");
    }

    return log_font;
  }

  void ApplyFontNameFromTextStyle(
      const RendererStyle::TextStyle* text_style, LOGFONTW* font) {
    if (text_style == nullptr || font == nullptr ||
        !text_style->has_font_name() || text_style->font_name().empty()) {
      return;
    }

    const std::wstring font_name =
        mozc::win32::Utf8ToWide(text_style->font_name());
    if (!IsSafeLogFontFaceName(font_name)) {
      LOG(WARNING) << "Skip unsafe candidate/ruby font: "
                  << text_style->font_name();
      return;
    }

    wcscpy_s(font->lfFaceName, font_name.c_str());
  }

  LOGFONTW GetLogFont(TextRenderer::FONT_TYPE type, uint32_t dpi) {
    LOGFONTW font = GetMessageBoxLogFont(dpi);

    RendererStyle style;
    GetScaledRendererStyle(&style, dpi);

    switch (type) {
      case TextRenderer::FONTSET_SHORTCUT: {
        const RendererStyle::TextStyle* text_style =
            GetTextStyleOrNull(style, kShortcutTextStyleIndex);
        if (text_style != nullptr && text_style->has_font_size()) {
          font.lfHeight =
              -static_cast<int>(std::lround(text_style->font_size()));
        } else {
          font.lfHeight += (font.lfHeight > 0 ? 2 : -2);
        }
        ApplyFontNameFromTextStyle(text_style, &font);
        font.lfWeight = FW_NORMAL;
        return font;
      }

      case TextRenderer::FONTSET_CANDIDATE: {
        const RendererStyle::TextStyle* text_style =
            GetTextStyleOrNull(style, kCandidateTextStyleIndex);
        if (text_style != nullptr && text_style->has_font_size()) {
          font.lfHeight =
              -static_cast<int>(std::lround(text_style->font_size()));
        } else {
          font.lfHeight += (font.lfHeight > 0 ? 2 : -2);
        }
        ApplyFontNameFromTextStyle(text_style, &font);
        font.lfWeight = FW_SEMIBOLD;
        return font;
      }

      case TextRenderer::FONTSET_DESCRIPTION: {
        const RendererStyle::TextStyle* text_style =
            GetTextStyleOrNull(style, kDescriptionTextStyleIndex);
        if (text_style != nullptr && text_style->has_font_size()) {
          font.lfHeight =
              -static_cast<int>(std::lround(text_style->font_size()));
        }
        ApplyFontNameFromTextStyle(text_style, &font);
        font.lfWeight = FW_NORMAL;
        return font;
      }

      case TextRenderer::FONTSET_FOOTER_INDEX:
      case TextRenderer::FONTSET_FOOTER_LABEL:
        if (style.footer_style().has_font_size()) {
          font.lfHeight =
              -static_cast<int>(std::lround(style.footer_style().font_size()));
        }
        ApplyFontNameFromTextStyle(&style.footer_style(), &font);
        font.lfWeight = FW_NORMAL;
        return font;

      case TextRenderer::FONTSET_FOOTER_SUBLABEL:
        if (style.footer_sub_label_style().has_font_size()) {
          font.lfHeight = -static_cast<int>(
              std::lround(style.footer_sub_label_style().font_size()));
        }
        ApplyFontNameFromTextStyle(&style.footer_sub_label_style(), &font);
        font.lfWeight = FW_NORMAL;
        return font;

      case TextRenderer::FONTSET_INFOLIST_CAPTION:
        if (style.infolist_style().caption_style().has_font_size()) {
          font.lfHeight = -static_cast<int>(std::lround(
              style.infolist_style().caption_style().font_size()));
        }
        ApplyFontNameFromTextStyle(&style.infolist_style().caption_style(),
                                   &font);
        font.lfWeight = FW_NORMAL;
        return font;

      case TextRenderer::FONTSET_INFOLIST_TITLE:
        if (style.infolist_style().title_style().has_font_size()) {
          font.lfHeight = -static_cast<int>(
              std::lround(style.infolist_style().title_style().font_size()));
        }
        ApplyFontNameFromTextStyle(&style.infolist_style().title_style(),
                                   &font);
        font.lfWeight = FW_NORMAL;
        return font;

      case TextRenderer::FONTSET_INFOLIST_DESCRIPTION:
        if (style.infolist_style().description_style().has_font_size()) {
          font.lfHeight = -static_cast<int>(std::lround(
              style.infolist_style().description_style().font_size()));
        }
        ApplyFontNameFromTextStyle(&style.infolist_style().description_style(),
                                   &font);
        font.lfWeight = FW_NORMAL;
        return font;

      default:
        LOG(DFATAL) << "Unknown type: " << type;
        return font;
    }
  }

DWORD GetGdiDrawTextStyle(TextRenderer::FONT_TYPE type) {
  switch (type) {
    case TextRenderer::FONTSET_CANDIDATE:
      return DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_DESCRIPTION:
      return DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_FOOTER_INDEX:
      return DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_FOOTER_LABEL:
      return DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_FOOTER_SUBLABEL:
      return DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_SHORTCUT:
      return DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_INFOLIST_CAPTION:
      return DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_INFOLIST_TITLE:
      return DT_LEFT | DT_SINGLELINE | DT_WORDBREAK | DT_EDITCONTROL |
             DT_NOPREFIX;
    case TextRenderer::FONTSET_INFOLIST_DESCRIPTION:
      return DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;
    default:
      LOG(DFATAL) << "Unknown type: " << type;
      return 0;
  }
}

class GdiTextRenderer : public TextRenderer {
 public:
  explicit GdiTextRenderer(uint32_t dpi)
      : render_info_(SIZE_OF_FONT_TYPE),
        mem_dc_(::CreateCompatibleDC(nullptr)),
        dpi_(dpi) {
    OnThemeChanged();
  }

 private:
  struct RenderInfo {
    RenderInfo() : color(0), style(0) {}
    COLORREF color;
    DWORD style;
    wil::unique_hfont font;
  };

  // TextRenderer overrides:
  void OnThemeChanged() override {
    // delete old fonts
    for (size_t i = 0; i < SIZE_OF_FONT_TYPE; ++i) {
      if (render_info_[i].font.is_valid()) {
        render_info_[i].font.reset();
      }
    }

    for (size_t i = 0; i < SIZE_OF_FONT_TYPE; ++i) {
      const auto font_type = static_cast<FONT_TYPE>(i);
      const LOGFONTW log_font = GetLogFont(font_type, dpi_);
      render_info_[i].style = GetGdiDrawTextStyle(font_type);
      render_info_[i].font.reset(::CreateFontIndirectW(&log_font));

      if (!render_info_[i].font.is_valid()) {
        const LOGFONTW fallback_log_font =
            GetLogFontWithDefaultFaceName(log_font, dpi_);
        render_info_[i].font.reset(::CreateFontIndirectW(&fallback_log_font));
      }

      render_info_[i].color = GetTextColor(font_type, dpi_);
    }
  }

  void OnDpiChanged(uint32_t dpi) override {
    if (dpi == dpi_) {
      return;
    }
    dpi_ = dpi;
    OnThemeChanged();
  }

  Size MeasureString(FONT_TYPE font_type,
                     const std::wstring_view str) const override {
    if (!render_info_[font_type].font.is_valid()) {
      return Size();
    }

    CRect rect;
    {
      const auto previous_font =
          wil::SelectObject(mem_dc_.get(), render_info_[font_type].font.get());
      ::DrawTextW(mem_dc_.get(), str.data(), str.length(), &rect,
                  DT_NOPREFIX | DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
    }
    return Size(rect.Width(), rect.Height());
  }

  Size MeasureStringMultiLine(FONT_TYPE font_type, const std::wstring_view str,
                              const int width) const override {
    if (!render_info_[font_type].font.is_valid()) {
      return Size();
    }

    CRect rect(0, 0, width, 0);
    {
      const auto previous_font =
          wil::SelectObject(mem_dc_.get(), render_info_[font_type].font.get());
      ::DrawTextW(mem_dc_.get(), str.data(), str.length(), &rect,
                  DT_NOPREFIX | DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    }
    return Size(rect.Width(), rect.Height());
  }

  void RenderText(HDC dc, const std::wstring_view text, const Rect& rect,
                  FONT_TYPE font_type) const override {
    std::vector<TextRenderingInfo> infolist;
    infolist.emplace_back(std::wstring(text), rect);
    RenderTextList(dc, infolist, font_type);
  }

  void RenderTextList(HDC dc,
                      const absl::Span<const TextRenderingInfo> display_list,
                      FONT_TYPE font_type) const override {
    const auto& render_info = render_info_[font_type];
    if (!render_info.font.is_valid()) {
      return;
    }

    const auto old_font = wil::SelectObject(dc, render_info.font.get());
    const auto previous_color = ::SetTextColor(dc, render_info.color);
    for (const TextRenderingInfo& info : display_list) {
      CRect rect = ToCRect(info.rect);
      ::DrawTextW(dc, info.text.data(), info.text.size(), &rect,
                  render_info.style);
    }
    ::SetTextColor(dc, previous_color);
  }

  std::vector<RenderInfo> render_info_;
  wil::unique_hdc mem_dc_;
  uint32_t dpi_;
};

class DirectWriteTextRenderer : public TextRenderer {
 public:
  DirectWriteTextRenderer(
      wil::com_ptr_nothrow<ID2D1Factory> d2d2_factory,
      wil::com_ptr_nothrow<IDWriteFactory> dwrite_factory,
      wil::com_ptr_nothrow<IDWriteGdiInterop> dwrite_interop, uint32_t dpi)
      : d2d2_factory_(std::move(d2d2_factory)),
        dwrite_factory_(std::move(dwrite_factory)),
        dwrite_interop_(std::move(dwrite_interop)),
        dpi_(dpi) {
    OnThemeChanged();
  }

  static std::unique_ptr<DirectWriteTextRenderer> Create(uint32_t dpi) {
    HRESULT hr = S_OK;
    wil::com_ptr_nothrow<ID2D1Factory> d2d_factory;
    hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                             __uuidof(ID2D1Factory), nullptr,
                             d2d_factory.put_void());
    if (FAILED(hr)) {
      return nullptr;
    }
    wil::com_ptr_nothrow<IDWriteFactory> dwrite_factory;
    hr = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                               __uuidof(IDWriteFactory),
                               dwrite_factory.put_unknown());
    if (FAILED(hr)) {
      return nullptr;
    }
    wil::com_ptr_nothrow<IDWriteGdiInterop> interop;
    hr = dwrite_factory->GetGdiInterop(interop.put());
    if (FAILED(hr)) {
      return nullptr;
    }
    return std::make_unique<DirectWriteTextRenderer>(std::move(d2d_factory),
                                                     std::move(dwrite_factory),
                                                     std::move(interop), dpi);
  }

 private:
  struct RenderInfo {
    RenderInfo() : color(0) {}
    COLORREF color;
    wil::com_ptr_nothrow<IDWriteTextFormat> format;
    wil::com_ptr_nothrow<IDWriteTextFormat> format_to_render;
  };

  // TextRenderer overrides:
  void OnThemeChanged() override {
    // delete old fonts
    render_info_.clear();
    render_info_.resize(SIZE_OF_FONT_TYPE);

    for (size_t i = 0; i < SIZE_OF_FONT_TYPE; ++i) {
      const auto font_type = static_cast<FONT_TYPE>(i);
      const LOGFONTW log_font = GetLogFont(font_type, dpi_);

      render_info_[i].color = GetTextColor(font_type, dpi_);
      render_info_[i].format = CreateFormatWithFallback(log_font);
      render_info_[i].format_to_render = CreateFormatWithFallback(log_font);

      if (render_info_[i].format == nullptr ||
          render_info_[i].format_to_render == nullptr) {
        LOG(ERROR) << "Failed to create DirectWrite text format: "
                  << static_cast<int>(font_type);
        continue;
      }

      const auto style = GetGdiDrawTextStyle(font_type);
      const auto render_font = render_info_[i].format_to_render;

      if ((style & DT_VCENTER) == DT_VCENTER) {
        render_font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      }
      if ((style & DT_LEFT) == DT_LEFT) {
        render_font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
      }
      if ((style & DT_CENTER) == DT_CENTER) {
        render_font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
      }
      if ((style & DT_RIGHT) == DT_RIGHT) {
        render_font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
      }
    }
  }

  void OnDpiChanged(uint32_t dpi) override {
    if (dpi == dpi_) {
      return;
    }
    dpi_ = dpi;
    OnThemeChanged();
  }

  // Retrieves the bounding box for a given string.
  Size MeasureString(FONT_TYPE font_type,
                     const std::wstring_view str) const override {
    return MeasureStringImpl(font_type, str, 0, false);
  }

  Size MeasureStringMultiLine(FONT_TYPE font_type, const std::wstring_view str,
                              const int width) const override {
    return MeasureStringImpl(font_type, str, width, true);
  }

  void RenderText(HDC dc, const std::wstring_view text, const Rect& rect,
                  FONT_TYPE font_type) const override {
    std::vector<TextRenderingInfo> infolist;
    infolist.emplace_back(std::wstring(text), rect);
    RenderTextList(dc, infolist, font_type);
  }

  void RenderTextList(HDC dc,
                      const absl::Span<const TextRenderingInfo> display_list,
                      FONT_TYPE font_type) const override {
    constexpr size_t kMaxTrial = 3;
    size_t trial = 0;
    while (true) {
      CreateRenderTargetIfNecessary();
      if (dc_render_target_ == nullptr) {
        // This is not a recoverable error.
        return;
      }
      const HRESULT hr = RenderTextListImpl(dc, display_list, font_type);
      if (hr == D2DERR_RECREATE_TARGET && trial < kMaxTrial) {
        // This is a recoverable error just by recreating the render target.
        dc_render_target_.reset();
        ++trial;
        continue;
      }
      // For other error codes (including S_OK and S_FALSE), or if we exceed the
      // maximum number of trials, we simply accept the result here.
      return;
    }
  }

  HRESULT RenderTextListImpl(
      HDC dc, const absl::Span<const TextRenderingInfo> display_list,
      FONT_TYPE font_type) const {
    if (display_list.empty() ||
        render_info_[font_type].format_to_render == nullptr) {
      return E_FAIL;
    }

    CRect total_rect;
    for (const auto& item : display_list) {
      const auto& item_rect = ToCRect(item.rect);
      total_rect.right = std::max(total_rect.right, item_rect.right);
      total_rect.bottom = std::max(total_rect.bottom, item_rect.bottom);
    }
    HRESULT hr = S_OK;
    hr = dc_render_target_->BindDC(dc, &total_rect);
    if (FAILED(hr)) {
      return hr;
    }
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> brush;
    hr = dc_render_target_->CreateSolidColorBrush(
        ToD2DColor(render_info_[font_type].color), brush.put());
    if (FAILED(hr)) {
      return hr;
    }
    constexpr D2D1_DRAW_TEXT_OPTIONS option =
        D2D1_DRAW_TEXT_OPTIONS_NONE | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;
    dc_render_target_->BeginDraw();
    dc_render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
    for (size_t i = 0; i < display_list.size(); ++i) {
      const auto& item = display_list[i];
      const D2D1_RECT_F render_rect = {
          static_cast<float>(item.rect.Left()),
          static_cast<float>(item.rect.Top()),
          static_cast<float>(item.rect.Right()),
          static_cast<float>(item.rect.Bottom()),
      };
      dc_render_target_->DrawText(
          item.text.data(), item.text.size(),
          render_info_[font_type].format_to_render.get(), render_rect,
          brush.get(), option);
    }
    return dc_render_target_->EndDraw();
  }

  Size MeasureStringImpl(FONT_TYPE font_type, const std::wstring_view str,
                         const int width, bool use_width) const {
    if (render_info_[font_type].format == nullptr) {
      return Size();
    }

    HRESULT hr = S_OK;
    constexpr FLOAT kLayoutLimit = 100000.0f;
    wil::com_ptr_nothrow<IDWriteTextLayout> layout;
    hr = dwrite_factory_->CreateTextLayout(
        str.data(), str.size(), render_info_[font_type].format.get(),
        (use_width ? width : kLayoutLimit), kLayoutLimit, layout.put());
    if (FAILED(hr)) {
      return Size();
    }
    DWRITE_TEXT_METRICS metrix = {};
    hr = layout->GetMetrics(&metrix);
    if (FAILED(hr)) {
      return Size();
    }
    return Size(ceilf(metrix.widthIncludingTrailingWhitespace),
                ceilf(metrix.height));
  }

  static D2D1_COLOR_F ToD2DColor(COLORREF color_ref) {
    D2D1_COLOR_F color;
    color.a = 1.0;
    color.r = GetRValue(color_ref) / 255.0f;
    color.g = GetGValue(color_ref) / 255.0f;
    color.b = GetBValue(color_ref) / 255.0f;
    return color;
  }

  bool IsUsableTextFormat(IDWriteTextFormat* format) const {
    if (format == nullptr) {
      return false;
    }

    constexpr wchar_t kSampleText[] = L"あア漢A1";
    const UINT32 sample_length = static_cast<UINT32>(
        std::char_traits<wchar_t>::length(kSampleText));

    wil::com_ptr_nothrow<IDWriteTextLayout> layout;
    const HRESULT layout_hr = dwrite_factory_->CreateTextLayout(
        kSampleText,
        sample_length,
        format,
        100000.0f,
        100000.0f,
        layout.put());

    if (FAILED(layout_hr) || layout == nullptr) {
      return false;
    }

    DWRITE_TEXT_METRICS metrics = {};
    const HRESULT metrics_hr = layout->GetMetrics(&metrics);
    if (FAILED(metrics_hr)) {
      return false;
    }

    return metrics.widthIncludingTrailingWhitespace > 0.0f &&
          metrics.height > 0.0f;
  }

  wil::com_ptr_nothrow<IDWriteTextFormat> CreateFormatWithFallback(
      const LOGFONTW& logfont) {
    wil::com_ptr_nothrow<IDWriteTextFormat> format = CreateFormat(logfont);
    if (IsUsableTextFormat(format.get())) {
      return format;
    }

    const LOGFONTW fallback_logfont =
        GetLogFontWithDefaultFaceName(logfont, dpi_);

    LOG(WARNING) << "Falling back to default renderer font.";

    wil::com_ptr_nothrow<IDWriteTextFormat> fallback_format =
        CreateFormat(fallback_logfont);
    if (IsUsableTextFormat(fallback_format.get())) {
      return fallback_format;
    }

    LOG(ERROR) << "Failed to create usable DirectWrite text format.";
    return nullptr;
  }

  wil::com_ptr_nothrow<IDWriteTextFormat> CreateFormat(const LOGFONT& logfont) {
    HRESULT hr = S_OK;
    wil::com_ptr_nothrow<IDWriteFont> font;
    hr = dwrite_interop_->CreateFontFromLOGFONT(&logfont, font.put());
    if (FAILED(hr)) {
      return nullptr;
    }
    wil::com_ptr_nothrow<IDWriteFontFamily> font_family;
    hr = font->GetFontFamily(font_family.put());
    if (FAILED(hr)) {
      return nullptr;
    }
    wil::com_ptr_nothrow<IDWriteLocalizedStrings> localized_family_names;
    hr = font_family->GetFamilyNames(localized_family_names.put());
    if (FAILED(hr)) {
      return nullptr;
    }
    UINT32 length_without_null = 0;
    hr = localized_family_names->GetStringLength(0, &length_without_null);
    if (FAILED(hr)) {
      return nullptr;
    }
    // |IDWriteLocalizedStrings::GetString()| requires the return buffer to be
    // large enough to store a result with the terminating null character.
    // https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritelocalizedstrings-getstring#parameters
    std::wstring family_name(size_t(length_without_null + 1), L'\0');
    hr = localized_family_names->GetString(0, family_name.data(),
                                           family_name.size());
    if (FAILED(hr)) {
      return nullptr;
    }
    family_name.resize(length_without_null);
    auto font_size = logfont.lfHeight;
    if (font_size < 0) {
      font_size = -font_size;
    } else {
      DWRITE_FONT_METRICS font_metrix = {};
      font->GetMetrics(&font_metrix);
      const auto cell_height =
          static_cast<float>(font_metrix.ascent + font_metrix.descent) /
          font_metrix.designUnitsPerEm;
      font_size /= cell_height;
    }

    wchar_t locale_name[LOCALE_NAME_MAX_LENGTH] = {};
    if (::GetUserDefaultLocaleName(locale_name, std::size(locale_name)) == 0) {
      return nullptr;
    }

    wil::com_ptr_nothrow<IDWriteTextFormat> format;
    hr = dwrite_factory_->CreateTextFormat(
        family_name.c_str(), nullptr, font->GetWeight(), font->GetStyle(),
        font->GetStretch(), font_size, locale_name, format.put());
    return format;
  }

  void CreateRenderTargetIfNecessary() const {
    if (dc_render_target_) {
      return;
    }
    const auto property = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    d2d2_factory_->CreateDCRenderTarget(&property, &dc_render_target_);
  }

  wil::com_ptr_nothrow<ID2D1Factory> d2d2_factory_;
  wil::com_ptr_nothrow<IDWriteFactory> dwrite_factory_;
  mutable wil::com_ptr_nothrow<ID2D1DCRenderTarget> dc_render_target_;
  wil::com_ptr_nothrow<IDWriteGdiInterop> dwrite_interop_;
  std::vector<RenderInfo> render_info_;
  uint32_t dpi_;
};

}  // namespace

// static
std::unique_ptr<TextRenderer> TextRenderer::Create(uint32_t dpi) {
  std::unique_ptr<TextRenderer> dwrite_text_renderer =
      DirectWriteTextRenderer::Create(dpi);
  if (dwrite_text_renderer != nullptr) {
    return dwrite_text_renderer;
  }
  return std::make_unique<GdiTextRenderer>(dpi);
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
