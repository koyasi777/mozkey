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

#include "renderer/win32/infolist_window.h"

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "base/coordinates.h"
#include "base/vlog.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_command.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/renderer_style_handler.h"
#include "renderer/win32/vertical_infolist_layout.h"
#include "renderer/win32/win32_dpi_util.h"
#include "renderer/win32/win32_renderer_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

using mozc::commands::Information;
using mozc::commands::InformationList;
using mozc::commands::Output;
using mozc::commands::SessionCommand;
using mozc::renderer::RendererStyle;

namespace {
const COLORREF kDefaultBackgroundColor = RGB(0xff, 0xff, 0xff);
const UINT_PTR kIdDelayShowHideTimer = 100;

void FillSolidRect(HDC dc, const RECT* rect, COLORREF color) {
  COLORREF old_color = ::SetBkColor(dc, color);
  if (old_color != CLR_INVALID) {
    ::ExtTextOut(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    ::SetBkColor(dc, old_color);
  }
}

}  // namespace

// ------------------------------------------------------------------------
// InfolistWindow
// ------------------------------------------------------------------------

InfolistWindow::InfolistWindow()
    : send_command_interface_(nullptr),
      candidate_window_(new commands::CandidateWindow),
      dpi_(::GetDpiForSystem()),
      text_renderer_(TextRenderer::Create(dpi_)),
      style_(new RendererStyle),
      layout_mode_(LayoutMode::kHorizontal),
      metrics_changed_(false),
      visible_(false) {
  GetScaledRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kCandidate, style_.get(), dpi_);
}

InfolistWindow::~InfolistWindow() {}

void InfolistWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == dpi_) {
    return;
  }
  dpi_ = dpi;
  GetScaledRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kCandidate, style_.get(), dpi_);
  text_renderer_->OnDpiChanged(dpi_);
  ClearBitmapCache();
}

void InfolistWindow::OnDestroy() {
  ClearBitmapCache();
  shadow_window_.Destroy();
  // PostQuitMessage may stop the message loop even though other
  // windows are not closed. WindowManager should close these windows
  // before process termination.
  ::PostQuitMessage(0);
}

BOOL InfolistWindow::OnEraseBkgnd(HDC dc) {
  // We do not have to erase background
  // because all pixels in client area will be drawn in the DoPaint method.
  return TRUE;
}

void InfolistWindow::OnGetMinMaxInfo(MINMAXINFO* min_max_info) {
  // Do not restrict the window size in case the candidate window must be
  // very small size.
  min_max_info->ptMinTrackSize.x = 1;
  min_max_info->ptMinTrackSize.y = 1;
  SetMsgHandled(TRUE);
}

void InfolistWindow::OnPaint(HDC dc) {
  if (dc != nullptr) {
    DoPaint(dc, true);
    return;
  }

  // The on-screen surface belongs to UpdateLayeredWindow. WM_PAINT only
  // validates the region and restores the cached layered surface if needed.
  wil::unique_hdc_paint paint_dc = wil::BeginPaint(this->m_hWnd);
  if (!cached_bitmap_valid_ && !RenderToBitmapCache()) {
    return;
  }
  PresentCachedBitmapImmediately();
}

void InfolistWindow::OnPrintClient(HDC dc, UINT /*uFlags*/) {
  if (dc != nullptr) {
    DoPaint(dc, true);
  }
}

bool InfolistWindow::IsVerticalLayout() const {
  return layout_mode_ == LayoutMode::kVertical;
}

Size InfolistWindow::DoPaint(HDC dc, bool draw_frame) {
  return IsVerticalLayout() ? DoPaintVertical(dc, draw_frame)
                            : DoPaintHorizontal(dc, draw_frame);
}

Size InfolistWindow::DoPaintHorizontal(HDC dc, bool draw_frame) {
  if (dc != nullptr) {
    ::SetBkMode(dc, TRANSPARENT);
  }
  const RendererStyle::InfolistStyle& infostyle = style_->infolist_style();
  const InformationList& usages = candidate_window_->usages();

  int ypos = infostyle.window_border();

  if ((dc != nullptr) && infostyle.has_caption_string()) {
    const RendererStyle::TextStyle& caption_style = infostyle.caption_style();
    const int caption_height = infostyle.caption_height();
    const Rect backgrounnd_rect(
        infostyle.window_border(), ypos,
        infostyle.window_width() - infostyle.window_border() * 2,
        caption_height);
    const CRect background_crect(
        backgrounnd_rect.Left(), backgrounnd_rect.Top(),
        backgrounnd_rect.Right(), backgrounnd_rect.Bottom());

    FillSolidRect(dc, &background_crect,
                  RGB(infostyle.caption_background_color().r(),
                      infostyle.caption_background_color().g(),
                      infostyle.caption_background_color().b()));

    const Rect caption_rect(
        infostyle.window_border() + infostyle.caption_padding() +
            caption_style.left_padding(),
        ypos + infostyle.caption_padding(),
        infostyle.window_width() - infostyle.window_border() * 2,
        caption_height);
    const std::wstring caption_str =
        mozc::win32::Utf8ToWide(infostyle.caption_string());

    text_renderer_->RenderText(dc, caption_str, caption_rect,
                               TextRenderer::FONTSET_INFOLIST_CAPTION);
  }
  ypos += infostyle.caption_height();

  for (int i = 0; i < usages.information_size(); ++i) {
    Size size = DoPaintHorizontalRow(dc, i, ypos);
    ypos += size.height;
  }
  ypos += infostyle.window_border();

  if (dc != nullptr && draw_frame) {
    const CRect rect(0, 0, infostyle.window_width(), ypos);
    const COLORREF border_color =
        RGB(infostyle.border_color().r(), infostyle.border_color().g(),
            infostyle.border_color().b());
    const int corner_radius = GetRendererWindowCornerRadiusInPixels(
        RendererStyleHandler::GetCandidateWindowCornerRadius(
            RendererStyleHandler::RendererStyleType::kCandidate),
        dpi_, rect.Width(), rect.Height());
    wil::unique_select_object old_brush = wil::SelectObject(
        dc, static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)));
    wil::unique_select_object old_pen =
        wil::SelectObject(dc, static_cast<HPEN>(::GetStockObject(DC_PEN)));
    ::SetDCPenColor(dc, border_color);
    if (corner_radius <= 0) {
      ::Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    } else {
      ::RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
                  corner_radius * 2, corner_radius * 2);
    }
  }

  return Size(style_->infolist_style().window_width(), ypos);
}

Size InfolistWindow::DoPaintHorizontalRow(HDC dc, int row, int ypos) {
  const RendererStyle::InfolistStyle& infostyle = style_->infolist_style();
  const InformationList& usages = candidate_window_->usages();
  const RendererStyle::TextStyle& title_style = infostyle.title_style();
  const RendererStyle::TextStyle& desc_style = infostyle.description_style();
  const int title_width =
      infostyle.window_width() - title_style.left_padding() -
      title_style.right_padding() - infostyle.window_border() * 2 -
      infostyle.row_rect_padding() * 2;
  const int desc_width = infostyle.window_width() - desc_style.left_padding() -
                         desc_style.right_padding() -
                         infostyle.window_border() * 2 -
                         infostyle.row_rect_padding() * 2;
  const Information& info = usages.information(row);

  const std::wstring title_str = mozc::win32::Utf8ToWide(info.title());
  const Size title_size = text_renderer_->MeasureStringMultiLine(
      TextRenderer::FONTSET_INFOLIST_TITLE, title_str, title_width);

  const std::wstring desc_str = mozc::win32::Utf8ToWide(info.description());
  const Size desc_size = text_renderer_->MeasureStringMultiLine(
      TextRenderer::FONTSET_INFOLIST_DESCRIPTION, desc_str, desc_width);

  int row_height =
      title_size.height + desc_size.height + infostyle.row_rect_padding() * 2;

  if (dc == nullptr) {
    return Size(0, row_height);
  }
  const Rect title_rect(
      infostyle.window_border() + infostyle.row_rect_padding() +
          title_style.left_padding(),
      ypos + infostyle.row_rect_padding(), title_width, title_size.height);
  const Rect desc_rect(
      infostyle.window_border() + infostyle.row_rect_padding() +
          desc_style.left_padding(),
      ypos + infostyle.row_rect_padding() + title_rect.size.height, desc_width,
      desc_size.height);

  const CRect title_back_crect(
      infostyle.window_border(), ypos,
      infostyle.window_width() - infostyle.window_border(),
      ypos + title_rect.size.height + infostyle.row_rect_padding());

  const CRect desc_back_crect(
      infostyle.window_border(),
      ypos + title_rect.size.height + infostyle.row_rect_padding(),
      infostyle.window_width() - infostyle.window_border(),
      ypos + title_rect.size.height + infostyle.row_rect_padding() +
          desc_rect.size.height + infostyle.row_rect_padding());

  if (usages.has_focused_index() && (row == usages.focused_index())) {
    const CRect selected_rect(
        infostyle.window_border(), ypos,
        infostyle.window_width() - infostyle.window_border(),
        ypos + title_rect.size.height + desc_rect.size.height +
            infostyle.row_rect_padding() * 2);
    FillSolidRect(dc, &selected_rect,
                  RGB(infostyle.focused_background_color().r(),
                      infostyle.focused_background_color().g(),
                      infostyle.focused_background_color().b()));
    ::SetDCBrushColor(dc, RGB(infostyle.focused_border_color().r(),
                              infostyle.focused_border_color().g(),
                              infostyle.focused_border_color().b()));
    ::FrameRect(dc, &selected_rect,
                static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
  } else {
    if (title_style.has_background_color()) {
      FillSolidRect(dc, &title_back_crect,
                    RGB(title_style.background_color().r(),
                        title_style.background_color().g(),
                        title_style.background_color().b()));
    } else {
      FillSolidRect(dc, &title_back_crect, RGB(255, 255, 255));
    }
    if (desc_style.has_background_color()) {
      FillSolidRect(dc, &desc_back_crect,
                    RGB(desc_style.background_color().r(),
                        desc_style.background_color().g(),
                        desc_style.background_color().b()));
    } else {
      FillSolidRect(dc, &desc_back_crect, RGB(255, 255, 255));
    }
  }

  text_renderer_->RenderText(dc, title_str, title_rect,
                             TextRenderer::FONTSET_INFOLIST_TITLE);
  text_renderer_->RenderText(dc, desc_str, desc_rect,
                             TextRenderer::FONTSET_INFOLIST_DESCRIPTION);
  return Size(0, row_height);
}

Size InfolistWindow::DoPaintVertical(HDC dc, bool draw_frame) {
  if (dc != nullptr) {
    ::SetBkMode(dc, TRANSPARENT);
  }

  const RendererStyle::InfolistStyle& infostyle = style_->infolist_style();
  const InformationList& usages = candidate_window_->usages();
  const int border = std::max(0, infostyle.window_border());
  const int style_row_padding = std::max(0, infostyle.row_rect_padding());
  const int window_height = std::max(1, infostyle.window_width());

  // Derive vertical-writing whitespace from the actual configured font at the
  // current DPI instead of hard-coding pixels. "日" is used only as an em-like
  // CJK advance probe; no content-dependent layout decision is made.
  const Size description_em = text_renderer_->MeasureStringVertical(
      TextRenderer::FONTSET_INFOLIST_DESCRIPTION, L"\u65E5");
  const int em_inline = std::max(1, description_em.height);
  const int em_cross = std::max(1, description_em.width);

  // About 0.2em cross-axis padding, 0.4em inline-axis padding, 0.25em between
  // title/body columns, and a full 1.0em body indent. The full CJK-em indent
  // gives the description a clear one-character visual step below its title,
  // matching conventional Japanese paragraph hierarchy without inserting
  // artificial spaces into the text.
  const int row_padding =
      std::max(style_row_padding, std::max(1, (em_cross + 4) / 5));
  const int vertical_padding =
      std::max(style_row_padding, std::max(1, (em_inline * 2 + 4) / 5));
  const int section_gap = std::max(1, (em_cross + 3) / 4);
  const int description_indent = em_inline;

  const int content_height = std::max(1, window_height - border * 2);
  const int text_height =
      std::max(1, content_height - vertical_padding * 2);

  const RendererStyle::TextStyle& caption_style = infostyle.caption_style();
  const RendererStyle::TextStyle& title_style = infostyle.title_style();
  const RendererStyle::TextStyle& desc_style = infostyle.description_style();

  int caption_width = std::max(0, infostyle.caption_height());
  if (infostyle.has_caption_string()) {
    const std::wstring caption =
        mozc::win32::Utf8ToWide(infostyle.caption_string());
    const Size caption_size = text_renderer_->MeasureStringVertical(
        TextRenderer::FONTSET_INFOLIST_CAPTION, caption);
    caption_width = std::max(
        caption_width,
        caption_size.width + infostyle.caption_padding() * 2 +
            caption_style.left_padding() + caption_style.right_padding());
  }

  std::vector<VerticalInfolistLayout::ItemMetrics> metrics;
  metrics.reserve(usages.information_size());
  for (int i = 0; i < usages.information_size(); ++i) {
    const Information& info = usages.information(i);
    VerticalInfolistLayout::ItemMetrics item;

    const std::wstring title = mozc::win32::Utf8ToWide(info.title());
    item.title_size = text_renderer_->MeasureStringVerticalWrapped(
        TextRenderer::FONTSET_INFOLIST_TITLE, title, text_height);
    item.title_left_padding = title_style.left_padding();
    item.title_right_padding = title_style.right_padding();

    item.description_top_indent =
        title.empty()
            ? 0
            : std::clamp(description_indent, 0, std::max(0, text_height - 1));
    const int description_height =
        std::max(1, text_height - item.description_top_indent);

    const std::wstring description =
        mozc::win32::Utf8ToWide(info.description());
    item.description_size = text_renderer_->MeasureStringVerticalWrapped(
        TextRenderer::FONTSET_INFOLIST_DESCRIPTION, description,
        description_height);
    item.description_left_padding = desc_style.left_padding();
    item.description_right_padding = desc_style.right_padding();

    metrics.push_back(item);
  }

  VerticalInfolistLayout::Parameters parameters;
  parameters.window_border = border;
  parameters.row_padding = row_padding;
  parameters.vertical_padding = vertical_padding;
  parameters.section_gap = section_gap;
  parameters.caption_width = caption_width;
  parameters.window_height = window_height;

  VerticalInfolistLayout layout;
  layout.Layout(metrics, parameters);
  const Size layout_size = layout.window_size();

  if (dc == nullptr) {
    return layout_size;
  }

  COLORREF default_background = kDefaultBackgroundColor;
  if (title_style.has_background_color()) {
    default_background =
        RGB(title_style.background_color().r(),
            title_style.background_color().g(),
            title_style.background_color().b());
  }
  const RECT full_rect = {0, 0, layout_size.width, layout_size.height};
  FillSolidRect(dc, &full_rect, default_background);

  const Rect caption_rect = layout.caption_rect();
  if (!caption_rect.IsRectEmpty()) {
    const CRect caption_background(
        caption_rect.Left(), caption_rect.Top(),
        caption_rect.Right(), caption_rect.Bottom());
    FillSolidRect(dc, &caption_background,
                  RGB(infostyle.caption_background_color().r(),
                      infostyle.caption_background_color().g(),
                      infostyle.caption_background_color().b()));

    if (infostyle.has_caption_string()) {
      const int caption_padding = std::max(0, infostyle.caption_padding());
      const int caption_inline_padding =
          std::max(caption_padding, vertical_padding);
      const int caption_left =
          caption_rect.Left() + caption_padding +
          std::max(0, caption_style.left_padding());
      const int caption_width_for_text = std::max(
          1, caption_rect.Right() - caption_padding -
                 std::max(0, caption_style.right_padding()) - caption_left);
      const int caption_top = caption_rect.Top() + caption_inline_padding;
      const int caption_height_for_text = std::max(
          1, caption_rect.Bottom() - caption_inline_padding - caption_top);
      const Rect caption_text_rect(
          caption_left, caption_top, caption_width_for_text,
          caption_height_for_text);
      text_renderer_->RenderTextVertical(
          dc, mozc::win32::Utf8ToWide(infostyle.caption_string()),
          caption_text_rect, TextRenderer::FONTSET_INFOLIST_CAPTION);
    }
  }

  for (int i = 0; i < usages.information_size(); ++i) {
    const Information& info = usages.information(i);
    const Rect item_rect = layout.GetItemRect(i);
    const Rect title_rect = layout.GetTitleRect(i);
    const Rect description_rect = layout.GetDescriptionRect(i);

    if (usages.has_focused_index() && i == usages.focused_index()) {
      const CRect selected_rect(
          item_rect.Left(), item_rect.Top(), item_rect.Right(),
          item_rect.Bottom());
      FillSolidRect(dc, &selected_rect,
                    RGB(infostyle.focused_background_color().r(),
                        infostyle.focused_background_color().g(),
                        infostyle.focused_background_color().b()));
      ::SetDCBrushColor(dc, RGB(infostyle.focused_border_color().r(),
                                infostyle.focused_border_color().g(),
                                infostyle.focused_border_color().b()));
      ::FrameRect(dc, &selected_rect,
                  static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
    }

    const std::wstring title = mozc::win32::Utf8ToWide(info.title());
    if (!title.empty() && !title_rect.IsRectEmpty()) {
      text_renderer_->RenderTextVerticalWrapped(
          dc, title, title_rect, TextRenderer::FONTSET_INFOLIST_TITLE);
    }

    const std::wstring description =
        mozc::win32::Utf8ToWide(info.description());
    if (!description.empty() && !description_rect.IsRectEmpty()) {
      text_renderer_->RenderTextVerticalWrapped(
          dc, description, description_rect,
          TextRenderer::FONTSET_INFOLIST_DESCRIPTION);
    }
  }

  if (draw_frame) {
    const CRect rect(0, 0, layout_size.width, layout_size.height);
    const COLORREF border_color =
        RGB(infostyle.border_color().r(), infostyle.border_color().g(),
            infostyle.border_color().b());
    const int corner_radius = GetRendererWindowCornerRadiusInPixels(
        RendererStyleHandler::GetCandidateWindowCornerRadius(
            RendererStyleHandler::RendererStyleType::kCandidate),
        dpi_, rect.Width(), rect.Height());
    wil::unique_select_object old_brush = wil::SelectObject(
        dc, static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)));
    wil::unique_select_object old_pen =
        wil::SelectObject(dc, static_cast<HPEN>(::GetStockObject(DC_PEN)));
    ::SetDCPenColor(dc, border_color);
    if (corner_radius <= 0) {
      ::Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    } else {
      ::RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
                  corner_radius * 2, corner_radius * 2);
    }
  }

  return layout_size;
}
void InfolistWindow::OnShowWindow(BOOL shown, UINT /*status*/) {
  if (!shown) {
    shadow_window_.Hide();
  }
}

void InfolistWindow::OnSettingChange(UINT uFlags, LPCTSTR /*lpszSection*/) {
  // Since TextRenderer uses dialog font to render,
  // we monitor font-related parameters to know when the font style is changed.
  switch (uFlags) {
    case 0x1049:  // = SPI_SETCLEARTYPE
    case SPI_SETFONTSMOOTHING:
    case SPI_SETFONTSMOOTHINGCONTRAST:
    case SPI_SETFONTSMOOTHINGORIENTATION:
    case SPI_SETFONTSMOOTHINGTYPE:
    case SPI_SETNONCLIENTMETRICS:
      metrics_changed_ = true;
      ClearBitmapCache();
      break;
    default:
      // We ignore other changes.
      break;
  }
}

void InfolistWindow::OnTimer(UINT_PTR nIDEvent) {
  if (nIDEvent != kIdDelayShowHideTimer) {
    return;
  }
  if (visible_) {
    DelayShow(0);
  } else {
    DelayHide(0);
  }
}

void InfolistWindow::DelayShow(UINT mseconds) {
  visible_ = true;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    // Prepare the layered pixels before exposing the window so delayed usage
    // UI cannot flash an empty or rectangular first frame.
    PresentCachedBitmapImmediately();
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SendMessageW(WM_NCACTIVATE, FALSE);
    UpdateEffectWindows();
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::DelayHide(UINT mseconds) {
  visible_ = false;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    shadow_window_.Hide();
    ShowWindow(SW_HIDE);
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::UpdateLayout(
    const commands::CandidateWindow& candidate_window) {
  UpdateLayout(candidate_window, LayoutMode::kHorizontal);
}

void InfolistWindow::UpdateLayout(
    const commands::CandidateWindow& candidate_window,
    LayoutMode layout_mode) {
  ClearBitmapCache();
  *candidate_window_ = candidate_window;

  // InfolistWindow caches both RendererStyle and TextRenderer.  Mozkey's
  // candidate/ruby font setting updates RendererStyle without sending
  // WM_SETTINGCHANGE, so refresh them whenever the infolist layout is updated.
  GetScaledRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kCandidate, style_.get(), dpi_);
  text_renderer_->OnThemeChanged();

  layout_mode_ = layout_mode;
  if (layout_mode_ == LayoutMode::kVertical &&
      (!text_renderer_->SupportsVerticalText(
           TextRenderer::FONTSET_INFOLIST_CAPTION) ||
       !text_renderer_->SupportsVerticalWrappedText(
           TextRenderer::FONTSET_INFOLIST_TITLE) ||
       !text_renderer_->SupportsVerticalWrappedText(
           TextRenderer::FONTSET_INFOLIST_DESCRIPTION))) {
    layout_mode_ = LayoutMode::kHorizontal;
  }

  metrics_changed_ = false;
  RenderToBitmapCache();
}

bool InfolistWindow::RenderToBitmapCache() {
  ClearBitmapCache();
  if (metrics_changed_) {
    GetScaledRendererStyleForWindowType(
        RendererStyleHandler::RendererStyleType::kCandidate, style_.get(),
        dpi_);
    text_renderer_->OnThemeChanged();
    metrics_changed_ = false;
  }
  const Size layout_size = DoPaint(nullptr, false);
  if (layout_size.width <= 0 || layout_size.height <= 0) {
    return false;
  }

  HDC screen_dc = ::GetDC(nullptr);
  if (screen_dc == nullptr) {
    return false;
  }
  wil::unique_hdc memory_dc(::CreateCompatibleDC(screen_dc));
  uint32_t* pixels = nullptr;
  wil::unique_hbitmap bitmap(CreateRendererLayeredWindowBitmap(
      screen_dc, layout_size.width, layout_size.height, &pixels));
  ::ReleaseDC(nullptr, screen_dc);
  if (!memory_dc.is_valid() || !bitmap.is_valid() || pixels == nullptr) {
    return false;
  }

  std::fill_n(pixels, static_cast<size_t>(layout_size.width) *
                          static_cast<size_t>(layout_size.height),
              0u);
  wil::unique_select_object old_bitmap =
      wil::SelectObject(memory_dc.get(), bitmap.get());

  const RendererStyle::InfolistStyle& infostyle = style_->infolist_style();
  COLORREF background_color = kDefaultBackgroundColor;
  if (infostyle.title_style().has_background_color()) {
    const RendererStyle::RGBAColor& background =
        infostyle.title_style().background_color();
    background_color = RGB(background.r(), background.g(), background.b());
  }
  const RECT background_rect = {0, 0, layout_size.width, layout_size.height};
  FillSolidRect(memory_dc.get(), &background_rect, background_color);
  DoPaint(memory_dc.get(), false);
  ::GdiFlush();

  const int corner_radius = GetRendererWindowCornerRadiusInPixels(
      RendererStyleHandler::GetCandidateWindowCornerRadius(
          RendererStyleHandler::RendererStyleType::kCandidate),
      dpi_, layout_size.width, layout_size.height);
  const COLORREF border_color =
      RGB(infostyle.border_color().r(), infostyle.border_color().g(),
          infostyle.border_color().b());
  ApplyRoundedRectAlphaAndBorder(pixels, layout_size.width, layout_size.height,
                                 corner_radius, /*border_width=*/1,
                                 border_color);

  cached_bitmap_ = std::move(bitmap);
  cached_bitmap_size_ = layout_size;
  cached_bitmap_valid_ = true;
  return true;
}

void InfolistWindow::ClearBitmapCache() {
  cached_bitmap_.reset();
  cached_bitmap_size_ = Size(0, 0);
  cached_bitmap_valid_ = false;
}

void InfolistWindow::PresentCachedBitmapImmediately() {
  if (m_hWnd == nullptr || !::IsWindow(m_hWnd)) {
    return;
  }
  if (!cached_bitmap_valid_ && !RenderToBitmapCache()) {
    return;
  }
  if (!cached_bitmap_.is_valid() || cached_bitmap_size_.width <= 0 ||
      cached_bitmap_size_.height <= 0) {
    return;
  }
  const RendererStyleHandler::CandidateWindowEffectStyle effect_style =
      RendererStyleHandler::GetCandidateWindowEffectStyle(
          RendererStyleHandler::RendererStyleType::kCandidate);
  PresentRendererLayeredWindowBitmap(
      m_hWnd, cached_bitmap_.get(), cached_bitmap_size_.width,
      cached_bitmap_size_.height,
      RendererWindowOpacityToAlpha(effect_style.opacity_percent));
}

void InfolistWindow::UpdateEffectWindows() {
  if (m_hWnd == nullptr || !::IsWindow(m_hWnd)) {
    return;
  }
  const RendererStyleHandler::CandidateWindowEffectStyle effect_style =
      RendererStyleHandler::GetCandidateWindowEffectStyle(
          RendererStyleHandler::RendererStyleType::kCandidate);
  PresentCachedBitmapImmediately();
  RECT window_rect = {};
  if (!::GetWindowRect(m_hWnd, &window_rect)) {
    shadow_window_.Hide();
    return;
  }
  RendererWindowShadowStyle shadow_style;
  shadow_style.size = effect_style.shadow.size;
  shadow_style.opacity_percent = effect_style.shadow.opacity_percent;
  shadow_style.angle_degrees = effect_style.shadow.angle_degrees;
  shadow_style.distance = effect_style.shadow.distance;
  const int corner_radius = GetRendererWindowCornerRadiusInPixels(
      RendererStyleHandler::GetCandidateWindowCornerRadius(
          RendererStyleHandler::RendererStyleType::kCandidate),
      dpi_, window_rect.right - window_rect.left,
      window_rect.bottom - window_rect.top);
  shadow_window_.Update(m_hWnd, window_rect, dpi_, corner_radius, shadow_style,
                        shadow_z_order_anchor_);
}

void InfolistWindow::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  send_command_interface_ = send_command_interface;
}

Size InfolistWindow::GetLayoutSize() {
  if (cached_bitmap_valid_) {
    return cached_bitmap_size_;
  }
  return DoPaint(nullptr, false);
}
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
