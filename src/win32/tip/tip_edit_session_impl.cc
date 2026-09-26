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

#include "win32/tip/tip_edit_session_impl.h"

#include <inputscope.h>
#include <msctf.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "base/util.h"
#include "base/win32/com.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "config/config_handler.h"
#include "protocol/commands.pb.h"
#include "win32/base/conversion_mode_util.h"
#include "win32/base/input_state.h"
#include "win32/base/string_util.h"
#include "win32/tip/tip_composition_util.h"
#include "win32/tip/tip_display_attributes.h"
#include "win32/tip/tip_edit_session.h"
#include "win32/tip/tip_input_mode_manager.h"
#include "win32/tip/tip_private_context.h"
#include "win32/tip/tip_range_util.h"
#include "win32/tip/tip_status.h"
#include "win32/tip/tip_text_service.h"
#include "win32/tip/tip_thread_context.h"
#include "win32/tip/tip_ui_handler.h"
#include "win32/tip/tip_writing_direction.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

using ::mozc::commands::Output;
using ::mozc::commands::Preedit;
using ::mozc::commands::SessionCommand;
using ::mozc::commands::Status;
using CompositionMode = ::mozc::commands::CompositionMode;
using Segment = ::mozc::commands::Preedit::Segment;
using Annotation = ::mozc::commands::Preedit::Segment::Annotation;

bool IsGeckoHostProcess() {
  // Firefox, Floorp and other desktop Gecko derivatives load xul.dll into the
  // process that owns TSFTextStore.
  return ::GetModuleHandleW(L"xul.dll") != nullptr;
}

bool HasPendingRomanSegment(const Preedit& preedit) {
  for (const Preedit::Segment& segment : preedit.segment()) {
    if (segment.is_pending_roman()) {
      return true;
    }
  }
  return false;
}

int MaxColorChannelDelta(const COLORREF lhs, const COLORREF rhs) {
  const auto delta = [](const int a, const int b) {
    return a >= b ? a - b : b - a;
  };
  return std::max(
      delta(GetRValue(lhs), GetRValue(rhs)),
      std::max(delta(GetGValue(lhs), GetGValue(rhs)),
               delta(GetBValue(lhs), GetBValue(rhs))));
}

// Capture one wider strip, but analyze only the far outer edges. The center
// around the caret is deliberately ignored because transient selections and
// the pending glyph itself can be highly uniform and otherwise look like an
// excellent "background" sample.
constexpr int kBackgroundCaptureWidth = 192;
constexpr int kBackgroundCaptureHeight = 7;
constexpr int kBackgroundEdgeRegionWidth = 24;
constexpr int kBackgroundOneSidedCaretGap = 32;
constexpr size_t kBackgroundCapturePixelCount =
    kBackgroundCaptureWidth * kBackgroundCaptureHeight;
constexpr int kBackgroundClusterDelta = 12;
constexpr int kBackgroundRegionAgreementDelta = 12;
constexpr int kBackgroundSystemHighlightRejectDelta = 18;
constexpr size_t kBackgroundDominantPercent = 58;

using BackgroundPixels =
    std::array<COLORREF, kBackgroundCapturePixelCount>;

bool AnalyzeBackgroundRegion(
    const BackgroundPixels& pixels, const int x_begin, const int x_end,
    COLORREF* color) {
  if (color == nullptr || x_begin < 0 || x_end > kBackgroundCaptureWidth ||
      x_begin >= x_end) {
    return false;
  }

  const size_t region_pixel_count =
      static_cast<size_t>(x_end - x_begin) * kBackgroundCaptureHeight;
  size_t best_count = 0;
  COLORREF best_seed = RGB(0, 0, 0);

  for (int y = 0; y < kBackgroundCaptureHeight; ++y) {
    for (int x = x_begin; x < x_end; ++x) {
      const COLORREF seed =
          pixels[static_cast<size_t>(y) * kBackgroundCaptureWidth + x];
      size_t count = 0;
      for (int other_y = 0; other_y < kBackgroundCaptureHeight; ++other_y) {
        for (int other_x = x_begin; other_x < x_end; ++other_x) {
          const COLORREF candidate =
              pixels[static_cast<size_t>(other_y) *
                         kBackgroundCaptureWidth +
                     other_x];
          if (MaxColorChannelDelta(seed, candidate) <=
              kBackgroundClusterDelta) {
            ++count;
          }
        }
      }
      if (count > best_count) {
        best_count = count;
        best_seed = seed;
      }
    }
  }

  if (best_count * 100 <
      region_pixel_count * kBackgroundDominantPercent) {
    return false;
  }

  uint64_t red_sum = 0;
  uint64_t green_sum = 0;
  uint64_t blue_sum = 0;
  size_t supporting_count = 0;

  for (int y = 0; y < kBackgroundCaptureHeight; ++y) {
    for (int x = x_begin; x < x_end; ++x) {
      const COLORREF candidate =
          pixels[static_cast<size_t>(y) * kBackgroundCaptureWidth + x];
      if (MaxColorChannelDelta(best_seed, candidate) >
          kBackgroundClusterDelta) {
        continue;
      }
      red_sum += GetRValue(candidate);
      green_sum += GetGValue(candidate);
      blue_sum += GetBValue(candidate);
      ++supporting_count;
    }
  }

  if (supporting_count == 0) {
    return false;
  }

  *color = RGB(
      static_cast<BYTE>(red_sum / supporting_count),
      static_cast<BYTE>(green_sum / supporting_count),
      static_cast<BYTE>(blue_sum / supporting_count));
  return true;
}

bool SampleVisibleBackgroundAtRangeEnd(
    ITfContext* context, ITfRange* range,
    const TfEditCookie read_cookie, COLORREF* background,
    bool* surface_uniform) {
  if (context == nullptr || range == nullptr || background == nullptr ||
      surface_uniform == nullptr) {
    return false;
  }

  *surface_uniform = false;

  // Do not sample while the active selection itself can paint a custom
  // selection background around the caret. Treat this as a retryable,
  // non-uniform surface rather than caching selection pixels as editor color.
  BOOL range_empty = FALSE;
  if (FAILED(range->IsEmpty(read_cookie, &range_empty))) {
    return false;
  }
  if (range_empty == FALSE) {
    return true;
  }

  wil::com_ptr_nothrow<ITfContextView> view;
  if (FAILED(context->GetActiveView(&view)) || !view) {
    return false;
  }

  wil::com_ptr_nothrow<ITfRange> caret_range;
  if (FAILED(range->Clone(&caret_range)) || !caret_range) {
    return false;
  }
  if (FAILED(caret_range->Collapse(read_cookie, TF_ANCHOR_END))) {
    return false;
  }

  RECT caret_rect = {};
  BOOL clipped = FALSE;
  if (FAILED(view->GetTextExt(read_cookie, caret_range.get(), &caret_rect,
                              &clipped))) {
    return false;
  }
  if (clipped != FALSE) {
    // Clipped geometry is not trustworthy for screen sampling, but it is
    // commonly transient during layout/scroll/focus changes. Keep it
    // retryable at the next new-composition boundary.
    return true;
  }

  const int height = caret_rect.bottom - caret_rect.top;
  if (height <= 0) {
    return false;
  }

  const int virtual_left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int virtual_top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int virtual_right =
      virtual_left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int virtual_bottom =
      virtual_top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

  const int caret_center_x =
      caret_rect.left + (caret_rect.right - caret_rect.left) / 2;

  // Prefer a strip centered on the caret so the two analyzed regions are far
  // away on opposite sides. Near a virtual-screen edge, fall back to a strip
  // on one side, still leaving a gap before the first analyzed region.
  int capture_x = caret_center_x - kBackgroundCaptureWidth / 2;
  if (capture_x < virtual_left ||
      capture_x + kBackgroundCaptureWidth > virtual_right) {
    const int right_candidate =
        caret_rect.right + kBackgroundOneSidedCaretGap;
    const int left_candidate =
        caret_rect.left - kBackgroundOneSidedCaretGap -
        kBackgroundCaptureWidth;

    if (right_candidate >= virtual_left &&
        right_candidate + kBackgroundCaptureWidth <= virtual_right) {
      capture_x = right_candidate;
    } else if (left_candidate >= virtual_left &&
               left_candidate + kBackgroundCaptureWidth <= virtual_right) {
      capture_x = left_candidate;
    } else {
      return false;
    }
  }

  const int center_y = caret_rect.top + height / 2;
  int capture_y = center_y - kBackgroundCaptureHeight / 2;
  capture_y = std::max(
      virtual_top,
      std::min(capture_y,
               virtual_bottom - kBackgroundCaptureHeight));

  HDC screen_dc = ::GetDC(nullptr);
  if (screen_dc == nullptr) {
    return false;
  }

  HDC memory_dc = ::CreateCompatibleDC(screen_dc);
  if (memory_dc == nullptr) {
    ::ReleaseDC(nullptr, screen_dc);
    return false;
  }

  BITMAPINFO bitmap_info = {};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = kBackgroundCaptureWidth;
  bitmap_info.bmiHeader.biHeight = -kBackgroundCaptureHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* bitmap_bits = nullptr;
  HBITMAP bitmap = ::CreateDIBSection(
      screen_dc, &bitmap_info, DIB_RGB_COLORS, &bitmap_bits, nullptr, 0);
  if (bitmap == nullptr || bitmap_bits == nullptr) {
    if (bitmap != nullptr) {
      ::DeleteObject(bitmap);
    }
    ::DeleteDC(memory_dc);
    ::ReleaseDC(nullptr, screen_dc);
    return false;
  }

  HGDIOBJ old_bitmap = ::SelectObject(memory_dc, bitmap);
  if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR) {
    ::DeleteObject(bitmap);
    ::DeleteDC(memory_dc);
    ::ReleaseDC(nullptr, screen_dc);
    return false;
  }

  // Do not use CAPTUREBLT here. Layered/transient windows are not part of the
  // editor's own surface and can otherwise poison the cached background.
  const BOOL copied = ::BitBlt(
      memory_dc, 0, 0, kBackgroundCaptureWidth, kBackgroundCaptureHeight,
      screen_dc, capture_x, capture_y, SRCCOPY);

  BackgroundPixels pixels = {};
  if (copied) {
    const BYTE* source = static_cast<const BYTE*>(bitmap_bits);
    for (size_t i = 0; i < kBackgroundCapturePixelCount; ++i) {
      pixels[i] =
          RGB(source[i * 4 + 2], source[i * 4 + 1], source[i * 4 + 0]);
    }
  }

  ::SelectObject(memory_dc, old_bitmap);
  ::DeleteObject(bitmap);
  ::DeleteDC(memory_dc);
  ::ReleaseDC(nullptr, screen_dc);

  if (!copied) {
    return false;
  }

  COLORREF left_color = RGB(0, 0, 0);
  COLORREF right_color = RGB(0, 0, 0);
  const bool left_uniform =
      AnalyzeBackgroundRegion(
          pixels, 0, kBackgroundEdgeRegionWidth, &left_color);
  const bool right_uniform =
      AnalyzeBackgroundRegion(
          pixels,
          kBackgroundCaptureWidth - kBackgroundEdgeRegionWidth,
          kBackgroundCaptureWidth, &right_color);

  if (!left_uniform || !right_uniform ||
      MaxColorChannelDelta(left_color, right_color) >
          kBackgroundRegionAgreementDelta) {
    return true;
  }

  const COLORREF candidate_background = RGB(
      static_cast<BYTE>(
          (static_cast<int>(GetRValue(left_color)) +
           static_cast<int>(GetRValue(right_color))) /
          2),
      static_cast<BYTE>(
          (static_cast<int>(GetGValue(left_color)) +
           static_cast<int>(GetGValue(right_color))) /
          2),
      static_cast<BYTE>(
          (static_cast<int>(GetBValue(left_color)) +
           static_cast<int>(GetBValue(right_color))) /
          2));

  // The default Windows selection highlight is commonly #0078D4. If a
  // transient selection fills both analyzed regions, geometric agreement alone
  // is not enough. Reject a candidate close to the current system highlight;
  // the one-shot typing retry can then sample again after selection settles.
  if (MaxColorChannelDelta(
          candidate_background, ::GetSysColor(COLOR_HIGHLIGHT)) <=
      kBackgroundSystemHighlightRejectDelta) {
    return true;
  }

  *background = candidate_background;
  *surface_uniform = true;
  return true;
}

constexpr uint32_t kDefaultPendingRomanDimnessPercent = 75;
constexpr uint32_t kMaxPendingRomanDimnessPercent = 90;

bool IsPendingRomanDimmingEnabled() {
  const auto current_config = config::ConfigHandler::GetSharedConfig();
  return current_config != nullptr &&
         current_config->dim_pending_roman_input();
}

uint32_t GetPendingRomanDimnessPercent() {
  const auto current_config = config::ConfigHandler::GetSharedConfig();
  if (!current_config) {
    return kDefaultPendingRomanDimnessPercent;
  }
  return std::min(current_config->pending_roman_dimness_percent(),
                  kMaxPendingRomanDimnessPercent);
}

COLORREF CreateMutedPendingRomanTextColor(
    const COLORREF background, const uint32_t dimness_percent) {
  const int r = GetRValue(background);
  const int g = GetGValue(background);
  const int b = GetBValue(background);

  const int luma = (299 * r + 587 * g + 114 * b) / 1000;
  const int contrast_percent =
      100 - static_cast<int>(
                std::min(dimness_percent,
                         kMaxPendingRomanDimnessPercent));
  constexpr int kPercentDenominator = 100;

  const auto blend_toward =
      [contrast_percent](const int component, const int target) -> BYTE {
    return static_cast<BYTE>(
        component +
        ((target - component) * contrast_percent) /
            kPercentDenominator);
  };

  const int target = luma < 128 ? 255 : 0;
  return RGB(blend_toward(r, target),
             blend_toward(g, target),
             blend_toward(b, target));
}

enum class GeckoSurfaceSamplePhase {
  // Focus can arrive before Gecko has finished layout/paint. A hard sampling
  // failure gets one retry when the first real composition begins.
  kFocusPrewarm,
  // A composition boundary is a safe recovery point. A previously non-uniform
  // surface may be sampled once here, but never repeatedly inside the same
  // composition.
  kNewComposition,
  // Existing-composition updates must not trigger screen reads after a failed
  // sample. This keeps the per-key hot path free of repeated BitBlt calls.
  kExistingComposition,
};

bool EnsureGeckoDisplayCompatibility(
    TipTextService* text_service, ITfContext* context, ITfRange* sample_range,
    const TfEditCookie read_cookie, const GeckoSurfaceSamplePhase phase) {
  if (text_service == nullptr || context == nullptr || sample_range == nullptr) {
    ClearGeckoDisplayCompatibilityBackground();
    SetPendingRomanDisplayAttributeCompatibilityFallback();
    return false;
  }

  TipPrivateContext* private_context =
      text_service->GetPrivateContext(context);

  // The feature is opt-in. When disabled, perform no screen capture at all and
  // discard any old sample so re-enabling cannot revive stale background data.
  if (!IsPendingRomanDimmingEnabled()) {
    if (private_context != nullptr) {
      private_context->ClearPendingRomanDisplayColors();
    }
    ClearGeckoDisplayCompatibilityBackground();
    SetPendingRomanDisplayAttributeCompatibilityFallback();
    return false;
  }

  if (!IsGeckoHostProcess()) {
    ClearGeckoDisplayCompatibilityBackground();
    return false;
  }

  if (private_context == nullptr) {
    ClearGeckoDisplayCompatibilityBackground();
    return false;
  }

  COLORREF text_color = RGB(0, 0, 0);
  COLORREF background = RGB(0, 0, 0);
  bool surface_uniform = false;
  bool retry_allowed = false;
  const bool has_cached_sample =
      private_context->GetPendingRomanDisplayColors(
          &text_color, &background, &surface_uniform, &retry_allowed);

  if (has_cached_sample && surface_uniform) {
    SetGeckoDisplayCompatibilityBackground(background, true);
    return true;
  }

  if (has_cached_sample) {
    // Failed/non-uniform cache entries may be retried only at the next
    // composition boundary. Existing-composition updates never sample again.
    // A hard failure after typing starts clears retry_allowed below and remains
    // unavailable until the next focus transition.
    if (phase != GeckoSurfaceSamplePhase::kNewComposition ||
        !retry_allowed) {
      ClearGeckoDisplayCompatibilityBackground();
      return false;
    }
  }

  if (!SampleVisibleBackgroundAtRangeEnd(
          context, sample_range, read_cookie, &background,
          &surface_uniform)) {
    // A focus-time hard failure can be a layout/paint race, so permit one
    // composition-boundary retry. If the real typing-time screen read itself
    // fails, treat it as unavailable for the remainder of this focus.
    private_context->SetPendingRomanDisplayColorsUnavailable(
        phase == GeckoSurfaceSamplePhase::kFocusPrewarm);
    ClearGeckoDisplayCompatibilityBackground();
    return false;
  }

  if (surface_uniform) {
    text_color = CreateMutedPendingRomanTextColor(
        background, GetPendingRomanDimnessPercent());
  }

  // A successful but non-uniform read is not a permanent capability failure.
  // Keep it retryable so the next new composition can recover after transient
  // selection/layout/paint content disappears. A uniform sample becomes the
  // stable cache for the remainder of this focus.
  private_context->SetPendingRomanDisplayColors(
      text_color, background, surface_uniform,
      !surface_uniform);

  if (!surface_uniform) {
    ClearGeckoDisplayCompatibilityBackground();
    return false;
  }

  // A high-confidence flat surface is safe to reproduce explicitly. This
  // avoids guessing Gecko's internal Field color or color-scheme.
  SetGeckoDisplayCompatibilityBackground(background, true);
  return true;
}

void PreparePendingRomanDisplayAttribute(
    TipTextService* text_service, ITfContext* context,
    const Preedit& preedit) {
  if (!HasPendingRomanSegment(preedit)) {
    return;
  }

  if (!IsGeckoHostProcess()) {
    SetPendingRomanDisplayAttributeSystemGray();
    return;
  }

  TipPrivateContext* private_context =
      text_service->GetPrivateContext(context);
  COLORREF text_color = RGB(0, 0, 0);
  COLORREF background = RGB(0, 0, 0);
  bool surface_uniform = false;
  bool retry_allowed = false;
  if (private_context != nullptr &&
      private_context->GetPendingRomanDisplayColors(
          &text_color, &background, &surface_uniform, &retry_allowed) &&
      surface_uniform) {
    // Pending foreground is only used together with a trusted explicit
    // background. If the surface is not trustworthy, use the no-color
    // compatibility fallback rather than triggering Gecko's Field fill.
    SetPendingRomanDisplayAttributeSampledColors(
        text_color, background, true);
    return;
  }

  SetPendingRomanDisplayAttributeCompatibilityFallback();
}

HRESULT SetReadingProperties(ITfContext* context, ITfRange* range,
                             const std::string& reading_string_utf8,
                             TfEditCookie write_cookie) {
  HRESULT result = S_OK;

  // Get out the reading property
  wil::com_ptr_nothrow<ITfProperty> reading_property;
  result = context->GetProperty(GUID_PROP_READING, &reading_property);
  if (FAILED(result)) {
    return result;
  }

  const std::wstring& canonical_reading_string =
      StringUtil::KeyToReading(reading_string_utf8);
  wil::unique_variant reading =
      wil::make_variant_bstr_nothrow(canonical_reading_string.c_str());
  return reading_property->SetValue(write_cookie, range, reading.addressof());
}

HRESULT ClearReadingProperties(ITfContext* context, ITfRange* range,
                               TfEditCookie write_cookie) {
  HRESULT result = S_OK;

  // Get out the reading property
  wil::com_ptr_nothrow<ITfProperty> reading_property;
  result = context->GetProperty(GUID_PROP_READING, &reading_property);
  if (FAILED(result)) {
    return result;
  }
  // Clear existing attributes.
  result = reading_property->Clear(write_cookie, range);
  if (FAILED(result)) {
    return result;
  }
  return result;
}

wil::com_ptr_nothrow<ITfComposition> CreateComposition(
    TipTextService* text_service, ITfContext* context,
    TfEditCookie write_cookie) {
  auto composition_context = ComQuery<ITfContextComposition>(context);
  if (!composition_context) {
    return nullptr;
  }
  auto insert_selection = ComQuery<ITfInsertAtSelection>(context);
  if (!insert_selection) {
    return nullptr;
  }
  wil::com_ptr_nothrow<ITfRange> insertion_pos;
  if (FAILED(insert_selection->InsertTextAtSelection(
          write_cookie, TF_IAS_QUERYONLY, nullptr, 0, &insertion_pos))) {
    return nullptr;
  }

  // Some TSF hosts expose correct writing-direction properties at the
  // insertion point but replace them with horizontal values on the composition
  // range after StartComposition. Snapshot the pre-composition direction in
  // the private context before giving the host a chance to rewrite attributes.
  TipPrivateContext* private_context = text_service->GetPrivateContext(context);
  if (private_context != nullptr) {
    WritingDirection direction = WritingDirection::kUnknown;
    TipRangeUtil::GetWritingDirection(insertion_pos.get(), write_cookie,
                                      &direction);
    private_context->SetCompositionWritingDirection(direction);
  }

  wil::com_ptr_nothrow<ITfComposition> composition;
  if (FAILED(composition_context->StartComposition(
          write_cookie, insertion_pos.get(),
          text_service->CreateCompositionSink(context).get(), &composition))) {
    if (private_context != nullptr) {
      private_context->ClearCompositionWritingDirection();
    }
    return nullptr;
  }
  return composition;
}

// Note: Committing a text is a tricky part in TSF/CUAS. Basically it should be
// done as following steps.
//   1. Create a composition (if not exists).
//   2. Replace the text stored in the composition range with the text to be
//      committed. Note that CUAS updates GCS_RESULTCLAUSE and
//      GCS_RESULTREADCLAUSE by using the segment structure of GUID_PROP_READING
//      property. For example, CUAS generates two segments for the following
//      reading text structure.
//        "今日は(きょうは)/晴天(せいてん)"
//   3. Call ITfComposition::ShiftStart to shrink the composition range. Note
//      that the text that is pushed out from the composition range is
//      interpreted as the "committed text".
//   4. Update the caret position explicitly. Note that some applications
//      such as WPF's TextBox do not update the caret position automatically
//      when a composition is committed.
// See also b/8406545 and b/9747361.
wil::com_ptr_nothrow<ITfComposition> CommitText(
    TipTextService* text_service, ITfContext* context,
    TfEditCookie write_cookie, wil::com_ptr_nothrow<ITfComposition> composition,
    const Output& output) {
  if (!composition) {
    composition = CreateComposition(text_service, context, write_cookie);
    if (!composition) {
      return nullptr;
    }
  }

  HRESULT result = S_OK;

  wil::com_ptr_nothrow<ITfRange> composition_range;
  result = composition->GetRange(&composition_range);
  if (FAILED(result)) {
    return nullptr;
  }

  std::wstring composition_text;
  TipRangeUtil::GetText(composition_range.get(), write_cookie,
                        &composition_text);

  // Make sure that |composition_text| begins with |result_text| so that
  // CUAS can generate an appropriate GCS_RESULTREADCLAUSE information.
  // See b/8406545
  const std::wstring result_text = Utf8ToWide(output.result().value());
  if (composition_text.find(result_text) != 0) {
    result = composition_range->SetText(write_cookie, 0, result_text.c_str(),
                                        result_text.size());
    if (FAILED(result)) {
      return nullptr;
    }
    result = SetReadingProperties(context, composition_range.get(),
                                  output.result().key(), write_cookie);
    if (FAILED(result)) {
      return nullptr;
    }
  }

  wil::com_ptr_nothrow<ITfRange> new_composition_start;
  result = composition_range->Clone(&new_composition_start);
  if (FAILED(result)) {
    return nullptr;
  }
  LONG moved = 0;
  result = new_composition_start->ShiftStart(write_cookie, result_text.size(),
                                             &moved, nullptr);
  if (FAILED(result)) {
    return nullptr;
  }
  result = new_composition_start->Collapse(write_cookie, TF_ANCHOR_START);
  if (FAILED(result)) {
    return nullptr;
  }
  result = composition->ShiftStart(write_cookie, new_composition_start.get());
  if (FAILED(result)) {
    return nullptr;
  }
  // We need to update the caret position manually for WPF's TextBox, where
  // caret position is not updated automatically when a composition text is
  // committed by ITfComposition::ShiftStart.
  result = TipRangeUtil::SetSelection(context, write_cookie,
                                      new_composition_start.get(), TF_AE_END);
  if (FAILED(result)) {
    return nullptr;
  }
  return composition;
}

HRESULT UpdateComposition(TipTextService* text_service, ITfContext* context,
                          wil::com_ptr_nothrow<ITfComposition> composition,
                          TfEditCookie write_cookie, const Output& output) {
  HRESULT result = S_OK;

  if (!output.has_preedit()) {
    if (composition) {
      wil::com_ptr_nothrow<ITfRange> composition_range;
      result = composition->GetRange(&composition_range);
      if (FAILED(result)) {
        return result;
      }
      BOOL is_empty = FALSE;
      result = composition_range->IsEmpty(write_cookie, &is_empty);
      if (FAILED(result)) {
        return result;
      }
      if (is_empty != TRUE) {
        std::wstring str;
        TipRangeUtil::GetText(composition_range.get(), write_cookie, &str);
        result = composition_range->SetText(write_cookie, 0, L"", 0);
        if (FAILED(result)) {
          return result;
        }
        result = ClearReadingProperties(context, composition_range.get(),
                                        write_cookie);
        if (FAILED(result)) {
          return result;
        }
      }
      result = composition->EndComposition(write_cookie);
      if (FAILED(result)) {
        // Keep the snapshot while the TSF composition may still be active.
        return result;
      }
    }
    if (TipPrivateContext* private_context =
            text_service->GetPrivateContext(context);
        private_context != nullptr) {
      private_context->ClearCompositionWritingDirection();
    }
    return S_OK;
  }

  DCHECK(output.has_preedit());

  if (!composition) {
    auto insert_selection = ComQuery<ITfInsertAtSelection>(context);
    if (!insert_selection) {
      return E_FAIL;
    }
    wil::com_ptr_nothrow<ITfRange> insertion_pos;
    result = insert_selection->InsertTextAtSelection(
        write_cookie, TF_IAS_QUERYONLY, nullptr, 0, &insertion_pos);
    if (FAILED(result)) {
      return result;
    }

    // Usually this is already warm from the asynchronous focus edit session.
    // If the cached surface was transiently non-uniform, a new composition is
    // the only typing-time point where we allow one recovery sample.
    EnsureGeckoDisplayCompatibility(
        text_service, context, insertion_pos.get(), write_cookie,
        GeckoSurfaceSamplePhase::kNewComposition);

    composition = CreateComposition(text_service, context, write_cookie);
    if (!composition) {
      return E_FAIL;
    }
  }
  wil::com_ptr_nothrow<ITfRange> composition_range;
  result = composition->GetRange(&composition_range);
  if (FAILED(result)) {
    return result;
  }

  const Preedit& preedit = output.preedit();
  const std::wstring& preedit_text = StringUtil::ComposePreeditText(preedit);

  // Resolve the display-property interface before changing visible text. This
  // minimizes the interval between SetText() and assigning the pending style.
  wil::com_ptr_nothrow<ITfProperty> display_attribute;
  result = context->GetProperty(GUID_PROP_ATTRIBUTE, &display_attribute);
  if (FAILED(result)) {
    return result;
  }

  EnsureGeckoDisplayCompatibility(
      text_service, context, composition_range.get(), write_cookie,
      GeckoSurfaceSamplePhase::kExistingComposition);
  PreparePendingRomanDisplayAttribute(
      text_service, context, preedit);

  result = composition_range->SetText(write_cookie, 0, preedit_text.c_str(),
                                      preedit_text.size());
  if (FAILED(result)) {
    return result;
  }

  std::vector<int> segment_starts(preedit.segment_size());
  std::vector<int> segment_ends(preedit.segment_size());
  int offset = 0;
  for (int i = 0; i < preedit.segment_size(); ++i) {
    segment_starts[i] = offset;
    offset += WideCharsLen(preedit.segment(i).value());
    segment_ends[i] = offset;
  }

  auto apply_display_attribute = [&](const int i) -> HRESULT {
    const Preedit::Segment& segment = preedit.segment(i);
    const Preedit::Segment::Annotation& annotation = segment.annotation();

    TfGuidAtom attribute = TF_INVALID_GUIDATOM;
    if (segment.is_pending_roman()) {
      attribute = text_service->pending_roman_attribute();
    } else if (annotation == Preedit::Segment::UNDERLINE) {
      attribute = text_service->input_attribute();
    } else if (annotation == Preedit::Segment::HIGHLIGHT) {
      attribute = text_service->converted_attribute();
    } else {
      return S_FALSE;
    }

    wil::com_ptr_nothrow<ITfRange> segment_range;
    HRESULT hr = composition_range->Clone(&segment_range);
    if (FAILED(hr)) {
      return hr;
    }
    hr = segment_range->Collapse(write_cookie, TF_ANCHOR_START);
    if (FAILED(hr)) {
      return hr;
    }
    LONG shift = 0;
    hr = segment_range->ShiftEnd(
        write_cookie, segment_ends[i], &shift, nullptr);
    if (FAILED(hr)) {
      return hr;
    }
    hr = segment_range->ShiftStart(
        write_cookie, segment_starts[i], &shift, nullptr);
    if (FAILED(hr)) {
      return hr;
    }

    wil::unique_variant var;
    var.vt = VT_I4;
    var.lVal = attribute;
    return display_attribute->SetValue(
        write_cookie, segment_range.get(), var.addressof());
  };

  // Pending romaji is the visually time-sensitive range. Assign its display
  // attribute first, before normal preedit ranges or reading properties.
  for (int i = 0; i < preedit.segment_size(); ++i) {
    if (!preedit.segment(i).is_pending_roman()) {
      continue;
    }
    result = apply_display_attribute(i);
    if (FAILED(result)) {
      return result;
    }
  }
  for (int i = 0; i < preedit.segment_size(); ++i) {
    if (preedit.segment(i).is_pending_roman()) {
      continue;
    }
    result = apply_display_attribute(i);
    if (FAILED(result)) {
      return result;
    }
  }

  // Reading properties are not visual. Defer them until all display
  // attributes have been assigned.
  wil::com_ptr_nothrow<ITfProperty> reading_property;
  result = context->GetProperty(GUID_PROP_READING, &reading_property);
  if (FAILED(result)) {
    return result;
  }

  for (int i = 0; i < preedit.segment_size(); ++i) {
    const Preedit::Segment& segment = preedit.segment(i);
    const Preedit::Segment::Annotation& annotation = segment.annotation();
    if (!segment.is_pending_roman() &&
        annotation != Preedit::Segment::UNDERLINE &&
        annotation != Preedit::Segment::HIGHLIGHT) {
      continue;
    }
    if (!segment.has_key()) {
      continue;
    }

    wil::com_ptr_nothrow<ITfRange> segment_range;
    result = composition_range->Clone(&segment_range);
    if (FAILED(result)) {
      return result;
    }
    result = segment_range->Collapse(write_cookie, TF_ANCHOR_START);
    if (FAILED(result)) {
      return result;
    }
    LONG shift = 0;
    result = segment_range->ShiftEnd(
        write_cookie, segment_ends[i], &shift, nullptr);
    if (FAILED(result)) {
      return result;
    }
    result = segment_range->ShiftStart(
        write_cookie, segment_starts[i], &shift, nullptr);
    if (FAILED(result)) {
      return result;
    }

    const std::wstring& reading_string =
        StringUtil::KeyToReading(segment.key());
    wil::unique_variant reading =
        wil::make_variant_bstr_nothrow(reading_string.c_str());
    result = reading_property->SetValue(
        write_cookie, segment_range.get(), reading.addressof());
    if (FAILED(result)) {
      return result;
    }
  }

  // Update cursor.
  {
    std::string preedit_text;
    for (int i = 0; i < preedit.segment_size(); ++i) {
      preedit_text += preedit.segment(i).value();
    }

    wil::com_ptr_nothrow<ITfRange> cursor_range;
    result = composition_range->Clone(&cursor_range);
    if (FAILED(result)) {
      return result;
    }
    // |output.preedit().cursor()| is in the unit of UTF-32. We need to convert
    // it to UTF-16 for TSF.
    const uint32_t cursor_pos_utf16 =
        WideCharsLen(Util::Utf8SubString(preedit_text, 0, preedit.cursor()));

    result = cursor_range->Collapse(write_cookie, TF_ANCHOR_START);
    if (FAILED(result)) {
      return result;
    }
    LONG shift = 0;
    result =
        cursor_range->ShiftEnd(write_cookie, cursor_pos_utf16, &shift, nullptr);
    if (FAILED(result)) {
      return result;
    }
    result = cursor_range->ShiftStart(write_cookie, cursor_pos_utf16, &shift,
                                      nullptr);
    if (FAILED(result)) {
      return result;
    }
    result = TipRangeUtil::SetSelection(context, write_cookie,
                                        cursor_range.get(), TF_AE_END);
  }
  return result;
}

HRESULT UpdatePrivateContext(TipTextService* text_service, ITfContext* context,
                             TfEditCookie write_cookie, const Output& output) {
  TipPrivateContext* private_context = text_service->GetPrivateContext(context);
  if (private_context == nullptr) {
    return S_FALSE;
  }
  *private_context->mutable_last_output() = output;
  if (!output.has_status()) {
    return S_FALSE;
  }

  const Status& status = output.status();
  TipInputModeManager* input_mode_manager =
      text_service->GetThreadContext()->GetInputModeManager();
  const TipInputModeManager::NotifyActionSet action_set =
      input_mode_manager->OnReceiveCommand(
          status.activated(), status.comeback_mode(), status.mode());
  if ((action_set & TipInputModeManager::kNotifySystemOpenClose) ==
      TipInputModeManager::kNotifySystemOpenClose) {
    TipStatus::SetIMEOpen(text_service->GetThreadManager(),
                          text_service->GetClientID(),
                          input_mode_manager->GetEffectiveOpenClose());
  }

  if ((action_set & TipInputModeManager::kNotifySystemConversionMode) ==
      TipInputModeManager::kNotifySystemConversionMode) {
    const CompositionMode mozc_mode = static_cast<CompositionMode>(
        input_mode_manager->GetEffectiveConversionMode());
    uint32_t native_mode = 0;
    if (ConversionModeUtil::ToNativeMode(
            mozc_mode, private_context->input_behavior().prefer_kana_input,
            &native_mode)) {
      TipStatus::SetInputModeConversion(text_service->GetThreadManager(),
                                        text_service->GetClientID(),
                                        native_mode);
    }
  }
  return S_OK;
}

HRESULT UpdatePreeditAndComposition(TipTextService* text_service,
                                    ITfContext* context,
                                    TfEditCookie write_cookie,
                                    const Output& output) {
  wil::com_ptr_nothrow<ITfComposition> composition =
      TipCompositionUtil::GetComposition(context, write_cookie);

  // Clear the display attributes first.
  // TODO(https://github.com/google/mozc/discussions/1388): Revisit here.
  if (composition) {
    const HRESULT result = TipCompositionUtil::ClearDisplayAttributes(
        context, composition.get(), write_cookie);
    if (FAILED(result)) {
      return result;
    }
  }

  if (output.has_result()) {
    composition = CommitText(text_service, context, write_cookie,
                             std::move(composition), output);
    if (!composition) {
      return E_FAIL;
    }
  }

  return UpdateComposition(text_service, context, std::move(composition),
                           write_cookie, output);
}

HRESULT DoEditSessionInComposition(TipTextService* text_service,
                                   ITfContext* context,
                                   TfEditCookie write_cookie,
                                   const Output& output) {
  const HRESULT result =
      UpdatePrivateContext(text_service, context, write_cookie, output);
  if (FAILED(result)) {
    return result;
  }
  return UpdatePreeditAndComposition(text_service, context, write_cookie,
                                     output);
}

HRESULT DoEditSessionAfterComposition(TipTextService* text_service,
                                      ITfContext* context,
                                      TfEditCookie write_cookie,
                                      const Output& output) {
  return UpdatePrivateContext(text_service, context, write_cookie, output);
}

HRESULT OnEndEditImpl(TipTextService* text_service, ITfContext* context,
                      TfEditCookie write_cookie, ITfEditRecord* edit_record,
                      bool* update_ui) {
  bool dummy_bool = false;
  if (update_ui == nullptr) {
    update_ui = &dummy_bool;
  }
  *update_ui = false;

  HRESULT result = S_OK;

  {
    wil::com_ptr_nothrow<ITfRange> selection_range;
    TfActiveSelEnd active_sel_end = TF_AE_NONE;
    result = TipRangeUtil::GetDefaultSelection(
        context, write_cookie, &selection_range, &active_sel_end);
    if (FAILED(result)) {
      return result;
    }
    std::vector<InputScope> input_scopes;
    result = TipRangeUtil::GetInputScopes(selection_range.get(), write_cookie,
                                          &input_scopes);
    TipInputModeManager* input_mode_manager =
        text_service->GetThreadContext()->GetInputModeManager();
    const auto actions = input_mode_manager->OnChangeInputScope(input_scopes);
    if (actions == TipInputModeManager::kUpdateUI) {
      *update_ui = true;
    }
    // If the indicator is visible, update UI just in case.
    if (input_mode_manager->IsIndicatorVisible()) {
      *update_ui = true;
    }
  }

  wil::com_ptr_nothrow<ITfComposition> composition =
      TipCompositionUtil::GetComposition(context, write_cookie);
  if (!composition) {
    // Nothing to do.
    return S_OK;
  }

  wil::com_ptr_nothrow<ITfRange> composition_range;
  result = composition->GetRange(&composition_range);
  if (FAILED(result)) {
    return result;
  }

  BOOL selection_changed = FALSE;
  result = edit_record->GetSelectionStatus(&selection_changed);
  if (FAILED(result)) {
    return result;
  }
  if (selection_changed) {
    // When the selection is changed, make sure the new selection range is
    // covered by the composition range. Otherwise, terminate the composition.
    wil::com_ptr_nothrow<ITfRange> selected_range;
    TfActiveSelEnd active_sel_end = TF_AE_NONE;
    result = TipRangeUtil::GetDefaultSelection(
        context, write_cookie, &selected_range, &active_sel_end);
    if (FAILED(result)) {
      return result;
    }
    if (!TipRangeUtil::IsRangeCovered(write_cookie, selected_range.get(),
                                      composition_range.get())) {
      // We enqueue another edit session to sync the composition state between
      // the application and Mozc server because we are already in
      // ITfTextEditSink::OnEndEdit and some operations (e.g.,
      // ITfComposition::EndComposition) result in failure in this edit
      // session.
      result = TipEditSession::SubmitAsync(text_service, context);
      if (FAILED(result)) {
        return result;
      }
      // Cancels further operations.
      return S_OK;
    }
  }

  BOOL is_empty = FALSE;
  result = composition_range->IsEmpty(write_cookie, &is_empty);
  if (FAILED(result)) {
    return result;
  }
  if (is_empty) {
    // When the composition range is empty, we assume the composition is
    // canceled by the application or something. Actually CUAS does this when
    // it receives NI_COMPOSITIONSTR/CPS_CANCEL. You can see this as Excel's
    // auto-completion. If this happens, send REVERT command to the server to
    // keep the state consistent. See b/1793331 for details.

    // We enqueue another edit session to sync the composition state between
    // the application and Mozc server because we are already in
    // ITfTextEditSink::OnEndEdit and some operations (e.g.,
    // ITfComposition::EndComposition) result in failure in this edit session.
    result = TipEditSession::CancelCompositionAsync(text_service, context);
    *update_ui = false;
    if (FAILED(result)) {
      return result;
    }
  }
  return S_OK;
}

}  // namespace

void TipEditSessionImpl::ResetGeckoDisplayCompatibility() {
  ClearGeckoDisplayCompatibilityBackground();
  SetPendingRomanDisplayAttributeCompatibilityFallback();
}

void TipEditSessionImpl::PrewarmGeckoDisplayCompatibility(
    TipTextService* text_service, ITfContext* context,
    ITfRange* selection_range, const TfEditCookie read_cookie) {
  if (text_service == nullptr || context == nullptr ||
      selection_range == nullptr || !IsGeckoHostProcess()) {
    return;
  }

  // OnSetFocusAsync synchronously owns generation reset. This asynchronous
  // callback is population-only: if typing has already produced a sample, the
  // cache must survive unchanged. EnsureGeckoDisplayCompatibility therefore
  // either reuses that cache or samples only when the generation still needs it.
  EnsureGeckoDisplayCompatibility(
      text_service, context, selection_range, read_cookie,
      GeckoSurfaceSamplePhase::kFocusPrewarm);
}

HRESULT TipEditSessionImpl::OnEndEdit(TipTextService* text_service,
                                      ITfContext* context,
                                      TfEditCookie write_cookie,
                                      ITfEditRecord* edit_record) {
  bool update_ui = false;
  const HRESULT result = OnEndEditImpl(text_service, context, write_cookie,
                                       edit_record, &update_ui);
  if (update_ui) {
    TipEditSessionImpl::UpdateUI(text_service, context, write_cookie);
  }
  return result;
}

HRESULT TipEditSessionImpl::OnCompositionTerminated(
    TipTextService* text_service, ITfContext* context,
    ITfComposition* composition, TfEditCookie write_cookie) {
  if (text_service == nullptr) {
    return E_FAIL;
  }
  if (context == nullptr) {
    return E_FAIL;
  }

  // This callback is also the authoritative cleanup path when an application
  // terminates the composition externally.
  TipPrivateContext* private_context = text_service->GetPrivateContext(context);
  if (private_context != nullptr) {
    private_context->ClearCompositionWritingDirection();
  }

  // Clear the display attributes first.
  // TODO(https://github.com/google/mozc/discussions/1388): Revisit here.
  if (composition) {
    const HRESULT result = TipCompositionUtil::ClearDisplayAttributes(
        context, composition, write_cookie);
    if (FAILED(result)) {
      return result;
    }
  }

  SessionCommand command;
  command.set_type(SessionCommand::SUBMIT);
  Output output;
  if (private_context == nullptr) {
    return E_FAIL;
  }
  if (!private_context->GetClient()->SendCommand(command, &output)) {
    return E_FAIL;
  }
  const HRESULT result = DoEditSessionAfterComposition(text_service, context,
                                                       write_cookie, output);
  UpdateUI(text_service, context, write_cookie);
  return result;
}

HRESULT TipEditSessionImpl::UpdateContext(TipTextService* text_service,
                                          ITfContext* context,
                                          TfEditCookie write_cookie,
                                          const commands::Output& output) {
  const HRESULT result =
      DoEditSessionInComposition(text_service, context, write_cookie, output);
  UpdateUI(text_service, context, write_cookie);
  return result;
}

void TipEditSessionImpl::UpdateUI(TipTextService* text_service,
                                  ITfContext* context,
                                  TfEditCookie read_cookie) {
  TipUiHandler::Update(text_service, context, read_cookie);
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
