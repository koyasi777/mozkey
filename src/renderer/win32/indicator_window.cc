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

#include "renderer/win32/indicator_window.h"

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "absl/log/check.h"
#include "base/const.h"
#include "base/win32/wide_char.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/win32/win32_font_util.h"
#include "renderer/win32/win32_image_util.h"
#include "renderer/win32/win32_renderer_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

namespace {

using ATL::CWindow;
using ATL::CWindowImpl;
using ATL::CWinTraits;

using ::mozc::commands::Status;
typedef ::mozc::commands::RendererCommand::ApplicationInfo ApplicationInfo;

// 96 DPI is the default DPI in Windows.
constexpr int kDefaultDPI = 96;

// As Discussed in b/2317702, UI windows are disabled by default because it is
// hard for a user to find out what caused the problem than finding that the
// operations seems to be disabled on the UI window when
// SPI_GETACTIVEWINDOWTRACKING is enabled.
// TODO(yukawa): Support mouse operations before we add a GUI feature which
//     requires UI interaction by mouse and/or touch. (b/2954874)
typedef CWinTraits<WS_POPUP | WS_DISABLED, WS_EX_LAYERED | WS_EX_TOOLWINDOW |
                                               WS_EX_TOPMOST | WS_EX_NOACTIVATE>
    IndicatorWindowTraits;

struct Sprite {
  wil::unique_hbitmap bitmap;
  CPoint offset;
};

// Timer event IDs
const UINT_PTR kTimerEventFadeStart = 0;
const UINT_PTR kTimerEventFading = 1;

constexpr DWORD kStartFadingOutDelay = 850;  // msec
constexpr DWORD kFadingOutInterval = 16;     // msec
constexpr int kFadingOutAlphaDelta = 24;

double GetDPIScaling() {
  wil::unique_hdc desktop_dc(::GetDC(nullptr));
  const int dpi_x = ::GetDeviceCaps(desktop_dc.get(), LOGPIXELSX);
  return static_cast<double>(dpi_x) / kDefaultDPI;
}

enum class ColorScheme {
  kLight,
  kDark,
};

ColorScheme GetWindowsAppColorScheme() {
  // Windows Settings:
  // Personalization -> Colors -> Choose your default app mode
  //
  // AppsUseLightTheme:
  //   0 = Dark
  //   1 = Light
  //
  // If this registry value does not exist, use Light as a safe default.
  DWORD apps_use_light_theme = 1;
  DWORD size = sizeof(apps_use_light_theme);

  const LONG result = ::RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme",
      RRF_RT_REG_DWORD,
      nullptr,
      &apps_use_light_theme,
      &size);

  if (result != ERROR_SUCCESS) {
    return ColorScheme::kLight;
  }

  return apps_use_light_theme == 0 ? ColorScheme::kDark : ColorScheme::kLight;
}

bool IsImmersiveColorSetChange(LPCTSTR section) {
  return section != nullptr &&
         ::lstrcmpi(section, TEXT("ImmersiveColorSet")) == 0;
}

constexpr int kIndicatorModes[] = {
    commands::DIRECT,
    commands::HIRAGANA,
    commands::FULL_KATAKANA,
    commands::HALF_ASCII,
    commands::FULL_ASCII,
    commands::HALF_KATAKANA,
};

struct IndicatorColors {
  RGBColor inside;
  RGBColor frame;
  RGBColor blur;
  RGBColor label;
};

IndicatorColors GetAsciiIndicatorColors(ColorScheme color_scheme) {
  if (color_scheme == ColorScheme::kDark) {
    return {
        RGBColor(30, 38, 52),
        RGBColor(96, 130, 170),
        RGBColor(0, 0, 0),
        RGBColor(235, 242, 250),
    };
  }

  return {
      RGBColor(245, 248, 252),
      RGBColor(92, 116, 145),
      RGBColor(120, 130, 145),
      RGBColor(28, 38, 52),
  };
}

IndicatorColors GetHiraganaIndicatorColors(ColorScheme color_scheme) {
  if (color_scheme == ColorScheme::kDark) {
    return {
        RGBColor(20, 64, 61),
        RGBColor(70, 190, 180),
        RGBColor(0, 0, 0),
        RGBColor(245, 255, 255),
    };
  }

  return {
      RGBColor(235, 252, 248),
      RGBColor(36, 150, 140),
      RGBColor(105, 145, 138),
      RGBColor(18, 78, 72),
  };
}

IndicatorColors GetKatakanaIndicatorColors(ColorScheme color_scheme) {
  if (color_scheme == ColorScheme::kDark) {
    return {
        RGBColor(42, 44, 70),
        RGBColor(126, 135, 245),
        RGBColor(0, 0, 0),
        RGBColor(245, 245, 255),
    };
  }

  return {
      RGBColor(244, 244, 255),
      RGBColor(98, 108, 215),
      RGBColor(120, 120, 155),
      RGBColor(48, 52, 105),
  };
}

IndicatorColors GetIndicatorColors(int mode, ColorScheme color_scheme) {
  switch (mode) {
    case commands::HIRAGANA:
      return GetHiraganaIndicatorColors(color_scheme);

    case commands::FULL_KATAKANA:
    case commands::HALF_KATAKANA:
      return GetKatakanaIndicatorColors(color_scheme);

    case commands::DIRECT:
    case commands::HALF_ASCII:
    case commands::FULL_ASCII:
    default:
      return GetAsciiIndicatorColors(color_scheme);
  }
}

const char* GetIndicatorLabel(int mode) {
  switch (mode) {
    case commands::DIRECT:
      return "A";
    case commands::HIRAGANA:
      return "あ";
    case commands::FULL_KATAKANA:
      return "ア";
    case commands::HALF_ASCII:
      return "_A";
    case commands::FULL_ASCII:
      return "Ａ";
    case commands::HALF_KATAKANA:
      return "_ｱ";
    default:
      return "";
  }
}

}  // namespace

class IndicatorWindow::WindowImpl
    : public CWindowImpl<IndicatorWindow::WindowImpl, CWindow,
                         IndicatorWindowTraits> {
 public:
  DECLARE_WND_CLASS_EX(kIndicatorWindowClassName, 0, COLOR_WINDOW);
  WindowImpl()
      : current_image_(nullptr),
        alpha_(255),
        dpi_scaling_(GetDPIScaling()),
        color_scheme_(GetWindowsAppColorScheme()) {
    sprites_.resize(commands::NUM_OF_COMPOSITIONS);
  }
  WindowImpl(const WindowImpl&) = delete;
  WindowImpl& operator=(const WindowImpl&) = delete;

  BEGIN_MSG_MAP(WindowImpl)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
  MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeChanged)
  END_MSG_MAP()

  void OnUpdate(const commands::RendererCommand& command,
                LayoutManager* layout_manager) {
    KillTimer(kTimerEventFading);
    KillTimer(kTimerEventFadeStart);

    bool visible = false;
    IndicatorWindowLayout indicator_layout;
    if (command.has_visible() && command.visible() &&
        command.has_application_info() &&
        command.application_info().has_indicator_info() &&
        command.application_info().indicator_info().has_status()) {
      const ApplicationInfo& app_info = command.application_info();
      visible =
          layout_manager->LayoutIndicatorWindow(app_info, &indicator_layout);
    }
    if (!visible) {
      HideIndicator();
      return;
    }
    DCHECK(command.has_application_info());
    DCHECK(command.application_info().has_indicator_info());
    DCHECK(command.application_info().indicator_info().has_status());
    const Status& status = command.application_info().indicator_info().status();

    alpha_ = 255;
    current_image_ = sprites_[commands::DIRECT].bitmap.get();
    CPoint offset = sprites_[commands::DIRECT].offset;
    if (!status.has_activated() || !status.has_mode() || !status.activated()) {
      current_image_ = sprites_[commands::DIRECT].bitmap.get();
      offset = sprites_[commands::DIRECT].offset;
    } else {
      const int mode = status.mode();
      switch (mode) {
        case commands::HIRAGANA:
        case commands::FULL_KATAKANA:
        case commands::HALF_ASCII:
        case commands::FULL_ASCII:
        case commands::HALF_KATAKANA:
          current_image_ = sprites_[mode].bitmap.get();
          offset = sprites_[mode].offset;
          break;
      }
    }
    if (current_image_ == nullptr) {
      HideIndicator();
      return;
    }
    top_left_ = CPoint(indicator_layout.window_rect.left - offset.x,
                       indicator_layout.window_rect.bottom - offset.y);
    UpdateWindow();

    // Start fading out.
    SetTimer(kTimerEventFadeStart, kStartFadingOutDelay);
  }

  void HideIndicator() {
    KillTimer(kTimerEventFading);
    KillTimer(kTimerEventFadeStart);
    ShowWindow(SW_HIDE);
  }

 private:
  void UpdateWindow() {
    BITMAP bm = {};
    ::GetObject(current_image_, sizeof(bm), &bm);
    CSize size(bm.bmWidth, bm.bmHeight);

    wil::unique_hdc dc(::CreateCompatibleDC(nullptr));

    // Fading out animation.
    CPoint top_left = top_left_;
    top_left.y += (255 - alpha_) / 32;

    CPoint src_left_top(0, 0);
    BLENDFUNCTION func = {AC_SRC_OVER, 0, alpha_, AC_SRC_ALPHA};

    {
      wil::unique_select_object old_bitmap =
          wil::SelectObject(dc.get(), current_image_);
      ::UpdateLayeredWindow(m_hWnd, nullptr, &top_left, &size, dc.get(),
                            &src_left_top, 0, &func, ULW_ALPHA);
    }
    ShowWindow(SW_SHOWNA);
  }

  void ReloadSprites() {
    current_image_ = nullptr;

    for (Sprite& sprite : sprites_) {
      sprite.bitmap.reset();
      sprite.offset = CPoint(0, 0);
    }

    for (size_t i = 0; i < std::size(kIndicatorModes); ++i) {
      LoadSprite(kIndicatorModes[i]);
    }
  }

  void ReloadSpritesIfColorSchemeChanged() {
    const ColorScheme new_color_scheme = GetWindowsAppColorScheme();
    if (new_color_scheme == color_scheme_) {
      return;
    }

    color_scheme_ = new_color_scheme;

    // current_image_ may point to a bitmap owned by sprites_.
    // Hide the window before destroying and recreating sprite bitmaps.
    HideIndicator();
    ReloadSprites();
  }

  LRESULT OnCreate(LPCREATESTRUCT create_struct) {
    EnableOrDisableWindowForWorkaround();
    ReloadSprites();
    return 1;
  }

  void OnTimer(UINT_PTR event_id) {
    switch (event_id) {
      case kTimerEventFadeStart:
        KillTimer(kTimerEventFadeStart);
        SetTimer(kTimerEventFading, kFadingOutInterval);
        break;
      case kTimerEventFading:
        alpha_ = std::max<int>(alpha_ - kFadingOutAlphaDelta, 0);
        if (alpha_ == 0) {
          KillTimer(kTimerEventFading);
        }
        UpdateWindow();
        break;
    }
  }

  void OnSettingChange(UINT flags, LPCTSTR lpszSection) {
    if (flags == SPI_SETACTIVEWINDOWTRACKING) {
      EnableOrDisableWindowForWorkaround();
    }

    if (IsImmersiveColorSetChange(lpszSection)) {
      ReloadSpritesIfColorSchemeChanged();
    }
  }

  void OnThemeChanged() {
    ReloadSpritesIfColorSchemeChanged();
  }

  void EnableOrDisableWindowForWorkaround() {
    // Disable the window if SPI_GETACTIVEWINDOWTRACKING is enabled.
    // See b/2317702 for details.
    // TODO(yukawa): Support mouse operations before we add a GUI feature which
    //   requires UI interaction by mouse and/or touch. (b/2954874)
    BOOL is_tracking_enabled = FALSE;
    if (::SystemParametersInfo(SPI_GETACTIVEWINDOWTRACKING, 0,
                               &is_tracking_enabled, 0)) {
      EnableWindow(!is_tracking_enabled);
    }
  }

  void LoadSprite(int mode) {
    BalloonImage::BalloonImageInfo info;
    LOGFONT logfont = GetMessageBoxLogFont(::GetDpiForSystem());
    info.label_font = mozc::win32::WideToUtf8(logfont.lfFaceName);

    info.rect_width = ceil(dpi_scaling_ * 36.0);
    info.rect_height = ceil(dpi_scaling_ * 28.0);
    info.corner_radius = dpi_scaling_ * 8.0;
    info.tail_height = 0.0;
    info.tail_width = 0.0;
    info.blur_sigma = dpi_scaling_ * 4.0;
    info.blur_alpha = color_scheme_ == ColorScheme::kDark ? 0.25 : 0.22;
    info.frame_thickness = dpi_scaling_ * 1.0;
    info.label_size = 12.0;
    info.blur_offset_x = 0;
    info.blur_offset_y = static_cast<int>(ceil(dpi_scaling_ * 1.0));

    const IndicatorColors colors = GetIndicatorColors(mode, color_scheme_);
    info.inside_color = colors.inside;
    info.frame_color = colors.frame;
    info.blur_color = colors.blur;
    info.label_color = colors.label;
    info.label = GetIndicatorLabel(mode);

    if (!info.label.empty()) {
      sprites_[mode].bitmap.reset(
          BalloonImage::Create(info, &sprites_[mode].offset));
    }
  }

  inline LRESULT OnCreate(UINT msg_id, WPARAM wparam, LPARAM lparam,
                          BOOL& handled) {
    return static_cast<LRESULT>(
        OnCreate(reinterpret_cast<LPCREATESTRUCT>(lparam)));
  }
  inline LRESULT OnTimer(UINT msg_id, WPARAM wparam, LPARAM lparam,
                         BOOL& handled) {
    OnTimer(static_cast<UINT_PTR>(wparam));
    return 0;
  }
  inline LRESULT OnSettingChange(UINT msg_id, WPARAM wparam, LPARAM lparam,
                                 BOOL& handled) {
    OnSettingChange(static_cast<UINT>(wparam),
                    reinterpret_cast<LPCTSTR>(lparam));
    return 0;
  }

  inline LRESULT OnThemeChanged(UINT msg_id, WPARAM wparam, LPARAM lparam,
                                BOOL& handled) {
    OnThemeChanged();
    return 0;
  }

  HBITMAP current_image_;
  CPoint top_left_;
  BYTE alpha_;
  double dpi_scaling_;
  ColorScheme color_scheme_;
  std::vector<Sprite> sprites_;
};

IndicatorWindow::IndicatorWindow() : impl_(new WindowImpl) {}

IndicatorWindow::~IndicatorWindow() { impl_->DestroyWindow(); }

void IndicatorWindow::Initialize() {
  impl_->Create(nullptr);
  impl_->ShowWindow(SW_HIDE);
}

void IndicatorWindow::Destroy() { impl_->DestroyWindow(); }

void IndicatorWindow::OnUpdate(const commands::RendererCommand& command,
                               LayoutManager* layout_manager) {
  impl_->OnUpdate(command, layout_manager);
}

void IndicatorWindow::Hide() { impl_->HideIndicator(); }

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
