#include "renderer/win32/ruby_window.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <wil/resource.h>

#include "base/win32/win_util.h"
#include "base/win32/wide_char.h"
#include "config/config_handler.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/renderer_style_handler.h"
#include "renderer/window_util.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_dpi_util.h"
#include "renderer/win32/win32_font_util.h"
#include "renderer/win32/win32_renderer_util.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

// Theme-aware translucent pill UI.
constexpr int kFontPointSize = 13;
// Ruby spacing values use 144 DPI as their design baseline so the existing
// visual density is preserved on a 150% display while other monitors scale
// proportionally. Font, corner radius, and shadow keep their normal Windows
// DPI scaling independently.
constexpr uint32_t kRubySpacingDesignDpi = 144;

// Keep the overlay close to the preedit, but do not let it cover the glyphs.
constexpr int kFallbackLineHeight = 22;
constexpr int kMaxReasonableLineHeight = 512;
constexpr int kGeometryMargin = 64;
constexpr int kNearOriginTolerance = 2;
constexpr int kLargeJumpMinX = 320;
constexpr int kLargeJumpMinY = 240;
constexpr int kTransientGeometryRejectLimit = 2;

COLORREF ToColorRef(uint32_t rgb) {
  return RGB(static_cast<int>((rgb >> 16) & 0xff),
             static_cast<int>((rgb >> 8) & 0xff),
             static_cast<int>(rgb & 0xff));
}

RendererStyleHandler::RubyWindowStyle GetRubyWindowTheme() {
  return RendererStyleHandler::GetRubyWindowStyle();
}

int ScaleByPercent(int value, uint32_t percent) {
  if (value <= 0) {
    return 0;
  }
  return std::max(1, static_cast<int>(std::lround(
                         static_cast<double>(value) * percent / 100.0)));
}

int ScaleRubySpacing(int value, uint32_t percent, uint32_t dpi) {
  if (value <= 0) {
    return 0;
  }
  return std::max(1, static_cast<int>(std::lround(
                         static_cast<double>(value) * percent / 100.0 *
                         static_cast<double>(dpi) /
                         kRubySpacingDesignDpi)));
}

int GetRubyPaddingX(const RendererStyleHandler::RubyWindowStyle& theme,
                    uint32_t dpi) {
  return ScaleRubySpacing(
      static_cast<int>(theme.horizontal_padding), theme.size_percent, dpi);
}

int GetRubyPaddingY(const RendererStyleHandler::RubyWindowStyle& theme,
                    uint32_t dpi) {
  return ScaleRubySpacing(
      static_cast<int>(theme.vertical_padding), theme.size_percent, dpi);
}

int GetRubyContentPaddingX(
    const RendererStyleHandler::RubyWindowStyle& theme, uint32_t dpi,
    bool vertical_writing) {
  return vertical_writing ? GetRubyPaddingY(theme, dpi)
                          : GetRubyPaddingX(theme, dpi);
}

int GetRubyContentPaddingY(
    const RendererStyleHandler::RubyWindowStyle& theme, uint32_t dpi,
    bool vertical_writing) {
  return vertical_writing ? GetRubyPaddingX(theme, dpi)
                          : GetRubyPaddingY(theme, dpi);
}

int GetRubyFontPointSize() {
  return ScaleByPercent(
      kFontPointSize, RendererStyleHandler::GetRubyWindowStyle().size_percent);
}

bool IsLiveConversionRubyWindowEnabled() {
  const auto config = config::ConfigHandler::GetSharedConfig();
  return config == nullptr || config->show_live_conversion_ruby_window();
}

std::wstring GetDefaultRubyWindowFontFaceName(uint32_t dpi) {
  const LOGFONTW logfont = GetMessageBoxLogFont(dpi);
  const std::wstring font_name(logfont.lfFaceName);
  if (font_name.empty() || font_name.front() == L'@') {
    return L"Segoe UI";
  }
  return font_name;
}

std::wstring GetRubyWindowFontFaceName(uint32_t dpi) {
  const std::wstring default_font_name =
      GetDefaultRubyWindowFontFaceName(dpi);

  RendererStyle style;
  if (!RendererStyleHandler::GetRendererStyle(&style)) {
    return default_font_name;
  }

  const RendererStyle::TextStyle& candidate_style = style.candidate_style();
  if (!candidate_style.has_font_name() || candidate_style.font_name().empty()) {
    return default_font_name;
  }

  const std::wstring font_name =
      mozc::win32::Utf8ToWide(candidate_style.font_name());
  if (font_name.empty()) {
    return default_font_name;
  }

  if (font_name.front() == L'@' || font_name.size() >= LF_FACESIZE) {
    return default_font_name;
  }

  return font_name;
}

std::wstring ToWide(const std::string& text) {
  return mozc::win32::Utf8ToWide(text);
}

RECT GetWorkAreaForPoint(const POINT& point) {
  RECT work_area = {};
  HMONITOR monitor = ::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);

  MONITORINFO monitor_info = {};
  monitor_info.cbSize = sizeof(monitor_info);
  if (::GetMonitorInfo(monitor, &monitor_info)) {
    return monitor_info.rcWork;
  }

  ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work_area, 0);
  return work_area;
}

int ClampInt(int value, int min_value, int max_value) {
  if (max_value < min_value) {
    return min_value;
  }
  return std::min(std::max(value, min_value), max_value);
}

int GetPreeditWidth(const commands::RendererCommand& command) {
  if (!command.has_preedit_rectangle()) {
    return 0;
  }

  const commands::RendererCommand::Rectangle& rect =
      command.preedit_rectangle();
  return std::max(0, rect.right() - rect.left());
}

bool Intersects(const RECT& lhs, const RECT& rhs) {
  return lhs.left < rhs.right && rhs.left < lhs.right &&
         lhs.top < rhs.bottom && rhs.top < lhs.bottom;
}

bool IsUsableRubyRect(const RECT& rect, const RECT& work_area,
                      const RECT* avoid_rect) {
  if (rect.left >= rect.right || rect.top >= rect.bottom) {
    return false;
  }
  if (rect.top < work_area.top || rect.bottom > work_area.bottom) {
    return false;
  }
  if (avoid_rect != nullptr && Intersects(rect, *avoid_rect)) {
    return false;
  }
  return true;
}

bool IsReasonableWorkArea(const RECT& work_area) {
  return work_area.left < work_area.right && work_area.top < work_area.bottom;
}

int RectCenterX(const RECT& rect) { return (rect.left + rect.right) / 2; }

int RectCenterY(const RECT& rect) { return (rect.top + rect.bottom) / 2; }

}  // namespace

RubyWindow::RubyWindow() = default;

RubyWindow::~RubyWindow() {
  ResetFont();
}

void RubyWindow::Initialize() {
  if (!IsWindow()) {
    Create(nullptr);
  }
  if (text_renderer_ == nullptr) {
    text_renderer_ = TextRenderer::Create(dpi_);
  }

  ShowWindow(SW_HIDE);
}

void RubyWindow::Destroy() {
  shadow_window_.Destroy();
  if (IsWindow()) {
    DestroyWindow();
  }
}

void RubyWindow::HideWindowOnly() {
  shadow_window_.Hide();
  if (IsWindow()) {
    ShowWindow(SW_HIDE);
  }
}

void RubyWindow::ClearPlacementTracking() {
  has_last_target_identity_ = false;
  has_last_valid_geometry_ = false;
  transient_geometry_reject_count_ = 0;
  last_target_identity_ = TargetIdentity();
  last_valid_window_rect_ = {};
}

void RubyWindow::Hide() {
  ClearPlacementTracking();
  HideWindowOnly();
}

void RubyWindow::RaiseToTopmostWithoutActivation() {
  if (m_hWnd == nullptr || !::IsWindow(m_hWnd) || !::IsWindowVisible(m_hWnd)) {
    return;
  }
  SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool RubyWindow::BuildReadingText(
    const commands::RendererCommand& command,
    std::string* reading) const {
  reading->clear();

  if (!command.has_output()) {
    return false;
  }

  const commands::Output& output = command.output();
  if (!output.live_conversion()) {
    return false;
  }

  if (!output.has_preedit()) {
    return false;
  }

  const commands::Preedit& preedit = output.preedit();

  std::string value;
  for (int i = 0; i < preedit.segment_size(); ++i) {
    const commands::Preedit::Segment& segment = preedit.segment(i);

    value.append(segment.value());

    if (segment.has_key() && !segment.key().empty()) {
      reading->append(segment.key());
    } else {
      reading->append(segment.value());
    }
  }

  if (reading->empty()) {
    return false;
  }

  // Always show the ruby overlay during live conversion, even when the
  // visible text and reading are identical.  This keeps the user's raw kana
  // input visible while live conversion is active.
  return true;
}

bool RubyWindow::GetBasePosition(const commands::RendererCommand& command,
                                 const LayoutManager& layout_manager,
                                 bool vertical_writing, POINT* point,
                                 int* line_height,
                                 bool* from_preedit_rectangle) const {
  const auto fill_from_composition_target = [&]() {
    if (!command.has_application_info()) {
      return false;
    }

    const commands::RendererCommand::ApplicationInfo& app_info =
        command.application_info();
    if (!app_info.has_composition_target() ||
        !app_info.composition_target().has_top_left()) {
      return false;
    }

    const commands::RendererCommand::CharacterPosition& target =
        app_info.composition_target();
    const int target_line_height =
        target.has_line_height() ? static_cast<int>(target.line_height())
                                 : kFallbackLineHeight;
    if (target_line_height <= 0) {
      return false;
    }

    POINT physical_top_left = {};
    int physical_line_height = 0;
    if (layout_manager.GetCompositionTargetInPhysicalCoords(
            app_info, kFallbackLineHeight, &physical_top_left,
            &physical_line_height)) {
      *point = physical_top_left;
      *line_height = physical_line_height;
    } else {
      // Preserve the historical fallback for malformed or legacy commands
      // whose target window cannot be used for DPI virtualization.
      point->x = target.top_left().x();
      point->y = target.top_left().y();
      *line_height = target_line_height;
    }
    if (from_preedit_rectangle != nullptr) {
      *from_preedit_rectangle = false;
    }
    return true;
  };

  // Windows' preedit_rectangle is a horizontal-writing caret correction.
  // For vertical writing, composition_target carries the right-top anchor and
  // physical column width established by the TSF writing-direction path.
  if (vertical_writing && fill_from_composition_target()) {
    return true;
  }

  // Horizontal writing preserves the existing preedit-rectangle-first
  // placement contract.
  if (command.has_preedit_rectangle()) {
    const commands::RendererCommand::Rectangle& rect =
        command.preedit_rectangle();

    RECT physical_rect = {rect.left(), rect.top(), rect.right(), rect.bottom()};
    if (command.has_application_info()) {
      const commands::RendererCommand::ApplicationInfo& app_info =
          command.application_info();
      if (app_info.input_framework() ==
              commands::RendererCommand::ApplicationInfo::TSF &&
          app_info.has_target_window_handle() &&
          app_info.target_window_handle() != 0) {
        const HWND target_window =
            WinUtil::DecodeWindowHandle(app_info.target_window_handle());
        const RECT logical_rect = physical_rect;
        layout_manager.GetRectInPhysicalCoords(target_window, logical_rect,
                                               &physical_rect);
      }
    }

    const int height = physical_rect.bottom - physical_rect.top;
    if (height > 0) {
      point->x = physical_rect.left;
      point->y = physical_rect.top;
      *line_height = height;
      if (from_preedit_rectangle != nullptr) {
        *from_preedit_rectangle = true;
      }
      return true;
    }
  }

  return fill_from_composition_target();
}
bool RubyWindow::GetTargetIdentity(const commands::RendererCommand& command,
                                   TargetIdentity* identity) {
  if (identity == nullptr || !command.has_application_info()) {
    return false;
  }

  const commands::RendererCommand::ApplicationInfo& app_info =
      command.application_info();
  if (!app_info.has_target_window_handle() ||
      app_info.target_window_handle() == 0) {
    return false;
  }

  identity->process_id =
      app_info.has_process_id() ? app_info.process_id() : 0;
  identity->thread_id = app_info.has_thread_id() ? app_info.thread_id() : 0;
  identity->target_window_handle = app_info.target_window_handle();
  return true;
}

bool RubyWindow::IsSameTargetIdentity(const TargetIdentity& lhs,
                                      const TargetIdentity& rhs) {
  return lhs.process_id == rhs.process_id &&
         lhs.thread_id == rhs.thread_id &&
         lhs.target_window_handle == rhs.target_window_handle;
}

bool RubyWindow::KeepCurrentPlacement() const {
  return has_last_valid_geometry_ && m_hWnd != nullptr && ::IsWindow(m_hWnd) &&
         ::IsWindowVisible(m_hWnd);
}

bool RubyWindow::ShouldRejectTransientGeometry(
    const POINT& base_point, int line_height, const RECT& window_rect,
    const RECT& work_area, bool from_preedit_rectangle,
    bool target_changed) const {
  if (!IsReasonableWorkArea(work_area)) {
    return false;
  }

  if (line_height <= 0 || line_height > kMaxReasonableLineHeight) {
    return true;
  }

  if (base_point.x < work_area.left - kGeometryMargin ||
      base_point.x > work_area.right + kGeometryMargin ||
      base_point.y < work_area.top - kGeometryMargin ||
      base_point.y > work_area.bottom + kGeometryMargin) {
    return true;
  }

  const bool base_near_work_area_origin =
      base_point.x <= work_area.left + kNearOriginTolerance &&
      base_point.y <= work_area.top + kNearOriginTolerance;
  const bool window_near_work_area_origin =
      window_rect.left <= work_area.left + kNearOriginTolerance &&
      window_rect.top <= work_area.top + kNearOriginTolerance;
  if ((base_near_work_area_origin || window_near_work_area_origin) &&
      (!from_preedit_rectangle || target_changed || has_last_valid_geometry_)) {
    return true;
  }

  if (!target_changed && has_last_valid_geometry_) {
    const int work_width = work_area.right - work_area.left;
    const int work_height = work_area.bottom - work_area.top;
    const int max_jump_x = std::max(kLargeJumpMinX, work_width / 2);
    const int max_jump_y = std::max(kLargeJumpMinY, work_height / 2);
    const int dx = std::abs(RectCenterX(window_rect) -
                            RectCenterX(last_valid_window_rect_));
    const int dy = std::abs(RectCenterY(window_rect) -
                            RectCenterY(last_valid_window_rect_));
    if (dx > max_jump_x || dy > max_jump_y) {
      return true;
    }
  }

  return false;
}

void RubyWindow::ResetFont() {
  if (font_ != nullptr) {
    ::DeleteObject(font_);
    font_ = nullptr;
  }

  font_face_name_.clear();
  font_height_ = 0;
  font_weight_ = 0;
  ruby_text_color_ = 0xffffffff;
}

void RubyWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == 0 || dpi == dpi_) {
    return;
  }
  dpi_ = dpi;
  if (text_renderer_ != nullptr) {
    text_renderer_->OnDpiChanged(dpi_);
  }
  ResetFont();
}

void RubyWindow::UpdateFont() {
  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();
  const std::wstring default_font_name =
      GetDefaultRubyWindowFontFaceName(dpi_);
  const std::wstring font_name = GetRubyWindowFontFaceName(dpi_);
  const int font_height = -MulDiv(GetRubyFontPointSize(), dpi_, 72);
  const int font_weight = std::clamp(
      static_cast<int>(theme.font_weight), static_cast<int>(FW_THIN),
      static_cast<int>(FW_HEAVY));

  if (font_ != nullptr &&
      font_height_ == font_height &&
      font_weight_ == font_weight &&
      ruby_text_color_ == theme.text_color &&
      font_face_name_ == font_name) {
    return;
  }

  ResetFont();

  LOGFONTW logfont = {};
  logfont.lfHeight = font_height;
  logfont.lfWeight = font_weight;
  logfont.lfQuality = CLEARTYPE_QUALITY;
  logfont.lfCharSet = DEFAULT_CHARSET;
  logfont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  wcscpy_s(logfont.lfFaceName, font_name.c_str());

  font_ = ::CreateFontIndirectW(&logfont);

  std::wstring actual_font_name = font_name;
  if (font_ == nullptr && font_name != default_font_name) {
    wcscpy_s(logfont.lfFaceName, default_font_name.c_str());
    font_ = ::CreateFontIndirectW(&logfont);
    actual_font_name = default_font_name;
  }

  if (text_renderer_ != nullptr) {
    text_renderer_->OnThemeChanged();
  }

  if (font_ == nullptr) {
    return;
  }

  font_face_name_ = actual_font_name;
  font_height_ = font_height;
  font_weight_ = font_weight;
  ruby_text_color_ = theme.text_color;
}

SIZE RubyWindow::MeasureText(bool vertical_writing) const {
  SIZE size = {};
  if (text_.empty()) {
    return size;
  }

  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();
  if (vertical_writing) {
    if (text_renderer_ == nullptr ||
        !text_renderer_->SupportsVerticalText(TextRenderer::FONTSET_RUBY)) {
      return size;
    }
    const Size text_size = text_renderer_->MeasureStringVertical(
        TextRenderer::FONTSET_RUBY, text_);
    if (text_size.width <= 0 || text_size.height <= 0) {
      return size;
    }
    size.cx = text_size.width +
              GetRubyContentPaddingX(theme, dpi_, /*vertical_writing=*/true) *
                  2;
    size.cy = text_size.height +
              GetRubyContentPaddingY(theme, dpi_, /*vertical_writing=*/true) *
                  2;
    return size;
  }

  HDC dc = ::GetDC(nullptr);

  HFONT old_font = nullptr;
  if (font_ != nullptr) {
    old_font = static_cast<HFONT>(::SelectObject(dc, font_));
  }

  ::GetTextExtentPoint32W(dc, text_.data(), static_cast<int>(text_.size()),
                          &size);

  TEXTMETRICW text_metric = {};
  if (::GetTextMetricsW(dc, &text_metric)) {
    size.cy = std::max<LONG>(size.cy, text_metric.tmHeight);
  }

  if (old_font != nullptr) {
    ::SelectObject(dc, old_font);
  }

  ::ReleaseDC(nullptr, dc);

  size.cx += GetRubyContentPaddingX(
                 theme, dpi_, /*vertical_writing=*/false) *
             2;
  size.cy += GetRubyContentPaddingY(
                 theme, dpi_, /*vertical_writing=*/false) *
             2;
  return size;
}

void RubyWindow::OnUpdate(const commands::RendererCommand& command,
                          const LayoutManager& layout_manager,
                          const RECT* avoid_rect) {
  if (!command.visible()) {
    Hide();
    return;
  }

  if (!IsLiveConversionRubyWindowEnabled()) {
    Hide();
    return;
  }

  std::string reading;
  if (!BuildReadingText(command, &reading)) {
    Hide();
    return;
  }

  bool vertical_writing = false;
  if (command.has_application_info()) {
    vertical_writing =
        LayoutManager::GetWritingDirection(command.application_info()) ==
        LayoutManager::VERTICAL_WRITING;
  }

  TargetIdentity current_target_identity;
  const bool has_current_target_identity =
      GetTargetIdentity(command, &current_target_identity);
  const bool target_changed =
      has_current_target_identity &&
      (!has_last_target_identity_ ||
       !IsSameTargetIdentity(last_target_identity_, current_target_identity));
  if (target_changed) {
    last_target_identity_ = current_target_identity;
    has_last_target_identity_ = true;
    has_last_valid_geometry_ = false;
    transient_geometry_reject_count_ = 0;
    HideWindowOnly();
  } else if (has_current_target_identity) {
    last_target_identity_ = current_target_identity;
    has_last_target_identity_ = true;
  } else {
    has_last_target_identity_ = false;
    has_last_valid_geometry_ = false;
    transient_geometry_reject_count_ = 0;
  }

  POINT base_point = {};
  int line_height = kFallbackLineHeight;
  bool from_preedit_rectangle = false;
  if (!GetBasePosition(command, layout_manager, vertical_writing, &base_point,
                       &line_height, &from_preedit_rectangle)) {
    if (KeepCurrentPlacement()) {
      return;
    }
    has_last_valid_geometry_ = false;
    HideWindowOnly();
    return;
  }

  // The composition target is now in physical screen coordinates. Resolve
  // the destination monitor before measuring text so the first visible frame
  // already uses that monitor's DPI.
  UpdateDpi(GetDpiForPoint(base_point.x, base_point.y));
  UpdateFont();

  const std::wstring previous_text = text_;
  const SIZE previous_window_size = window_size_;
  const bool previous_vertical_writing = vertical_writing_;
  const auto restore_previous_content = [&]() {
    text_ = previous_text;
    window_size_ = previous_window_size;
    vertical_writing_ = previous_vertical_writing;
  };

  text_ = ToWide(reading);
  window_size_ = MeasureText(vertical_writing);
  if (window_size_.cx <= 0 || window_size_.cy <= 0) {
    restore_previous_content();
    Hide();
    return;
  }

  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();
  const int gap = ScaleRubySpacing(
      static_cast<int>(theme.composition_gap), theme.size_percent, dpi_);

  const RECT work_area = GetWorkAreaForPoint(base_point);

  int left = 0;
  int top = 0;
  RECT preferred_window_rect = {};
  RECT window_rect = {};

  if (vertical_writing) {
    // composition_target is the right-top corner of the active vertical column
    // and line_height is its physical width. Put the reading on the conventional
    // Japanese ruby side (right) first, with left as the fallback.
    const Rect work_rect(work_area.left, work_area.top,
                         work_area.right - work_area.left,
                         work_area.bottom - work_area.top);
    std::optional<Rect> avoid;
    if (avoid_rect != nullptr) {
      avoid.emplace(avoid_rect->left, avoid_rect->top,
                    avoid_rect->right - avoid_rect->left,
                    avoid_rect->bottom - avoid_rect->top);
    }

    const int text_top_offset =
        GetRubyContentPaddingY(theme, dpi_, /*vertical_writing=*/true);
    const Rect preferred_rect(base_point.x + gap,
                              base_point.y - text_top_offset,
                              window_size_.cx, window_size_.cy);
    preferred_window_rect =
        RECT{preferred_rect.Left(), preferred_rect.Top(),
             preferred_rect.Right(), preferred_rect.Bottom()};

    Rect placed_rect;
    if (!WindowUtil::GetRubyWindowRectForVerticalWriting(
            Point(base_point.x, base_point.y), line_height,
            Size(window_size_.cx, window_size_.cy), text_top_offset, gap,
            work_rect, avoid.has_value() ? &*avoid : nullptr, &placed_rect)) {
      restore_previous_content();
      const bool looks_like_transient_geometry =
          ShouldRejectTransientGeometry(
              base_point, line_height, preferred_window_rect, work_area,
              from_preedit_rectangle, target_changed);
      if (looks_like_transient_geometry && KeepCurrentPlacement()) {
        return;
      }
      has_last_valid_geometry_ = false;
      HideWindowOnly();
      return;
    }

    left = placed_rect.Left();
    top = placed_rect.Top();
    window_rect =
        RECT{placed_rect.Left(), placed_rect.Top(),
             placed_rect.Right(), placed_rect.Bottom()};
  } else {
    // Keep the left edge stable while the reading text grows. Centering a ruby
    // chip that is wider than the preedit makes the window expand both left and
    // right on every keystroke, which is visually noisy in live conversion.
    const int preedit_width = GetPreeditWidth(command);

    left = base_point.x;
    if (preedit_width > window_size_.cx) {
      left = base_point.x + (preedit_width - window_size_.cx) / 2;
    }
    left = ClampInt(left, work_area.left, work_area.right - window_size_.cx);

    const int above_top = base_point.y - window_size_.cy - gap;
    const int below_top = base_point.y + line_height + gap;

    const auto ruby_rect_at = [&](int candidate_top) {
      return RECT{left, candidate_top, left + window_size_.cx,
                  candidate_top + window_size_.cy};
    };

    const auto try_place = [&](int candidate_top, int* chosen_top) {
      const RECT rect = ruby_rect_at(candidate_top);
      if (!IsUsableRubyRect(rect, work_area, avoid_rect)) {
        return false;
      }
      *chosen_top = candidate_top;
      return true;
    };

    // Preserve the existing horizontal contract: above first, below fallback.
    preferred_window_rect = ruby_rect_at(above_top);
    const bool looks_like_transient_geometry = ShouldRejectTransientGeometry(
        base_point, line_height, preferred_window_rect, work_area,
        from_preedit_rectangle, target_changed);
    if (!try_place(above_top, &top) && !try_place(below_top, &top)) {
      restore_previous_content();
      if (looks_like_transient_geometry && KeepCurrentPlacement()) {
        return;
      }
      has_last_valid_geometry_ = false;
      HideWindowOnly();
      return;
    }

    window_rect =
        RECT{left, top, left + window_size_.cx, top + window_size_.cy};
  }
  if (ShouldRejectTransientGeometry(base_point, line_height, window_rect,
                                    work_area, from_preedit_rectangle,
                                    target_changed)) {
    restore_previous_content();
    ++transient_geometry_reject_count_;
    if (transient_geometry_reject_count_ <= kTransientGeometryRejectLimit) {
      if (KeepCurrentPlacement()) {
        return;
      }
      HideWindowOnly();
      return;
    }
  }
  transient_geometry_reject_count_ = 0;

  // Resize and position while still hidden. The layered surface is fully
  // prepared before ShowWindow so the first visible ruby frame is already
  // antialiased and complete.
  vertical_writing_ = vertical_writing;
  SetWindowPos(HWND_TOPMOST, left, top, window_size_.cx, window_size_.cy,
               SWP_NOACTIVATE);
  if (!RenderAndPresent()) {
    restore_previous_content();
    HideWindowOnly();
    return;
  }

  ShowWindow(SW_SHOWNOACTIVATE);

  const RendererStyleHandler::RubyWindowStyle current_theme =
      GetRubyWindowTheme();
  RendererWindowShadowStyle shadow_style;
  shadow_style.size = current_theme.shadow.size;
  shadow_style.opacity_percent = current_theme.shadow.opacity_percent;
  shadow_style.angle_degrees = current_theme.shadow.angle_degrees;
  shadow_style.distance = current_theme.shadow.distance;
  const int corner_radius = GetRendererWindowCornerRadiusInPixels(
      current_theme.corner_radius, dpi_, window_size_.cx, window_size_.cy);
  shadow_window_.Update(m_hWnd, window_rect, dpi_, corner_radius, shadow_style);

  last_valid_window_rect_ = window_rect;

  has_last_valid_geometry_ = true;
  if (has_current_target_identity) {
    last_target_identity_ = current_target_identity;
    has_last_target_identity_ = true;
  }

}

LRESULT RubyWindow::OnEraseBkgnd(UINT /*msg_id*/,
                                 WPARAM /*wparam*/,
                                 LPARAM /*lparam*/,
                                 BOOL& handled) {
  handled = TRUE;
  return TRUE;
}

LRESULT RubyWindow::OnShowWindow(UINT /*msg_id*/, WPARAM wparam,
                                 LPARAM /*lparam*/, BOOL& /*handled*/) {
  if (wparam == FALSE) {
    shadow_window_.Hide();
  }
  return 0;
}

LRESULT RubyWindow::OnPaint(UINT /*msg_id*/,
                            WPARAM /*wparam*/,
                            LPARAM /*lparam*/,
                            BOOL& /*handled*/) {
  PAINTSTRUCT ps = {};
  BeginPaint(&ps);
  EndPaint(&ps);
  return 0;
}

bool RubyWindow::RenderAndPresent() {
  if (m_hWnd == nullptr || !::IsWindow(m_hWnd) || window_size_.cx <= 0 ||
      window_size_.cy <= 0) {
    return false;
  }

  HDC screen_dc = ::GetDC(nullptr);
  if (screen_dc == nullptr) {
    return false;
  }
  wil::unique_hdc memory_dc(::CreateCompatibleDC(screen_dc));
  uint32_t* pixels = nullptr;
  wil::unique_hbitmap bitmap(CreateRendererLayeredWindowBitmap(
      screen_dc, window_size_.cx, window_size_.cy, &pixels));
  ::ReleaseDC(nullptr, screen_dc);
  if (!memory_dc.is_valid() || !bitmap.is_valid() || pixels == nullptr) {
    return false;
  }

  std::fill_n(pixels, static_cast<size_t>(window_size_.cx) *
                          static_cast<size_t>(window_size_.cy),
              0u);
  // A GDI bitmap can be selected into only one memory DC at a time. Keep the
  // drawing selection scoped so the bitmap is restored to the original DC
  // before PresentRendererLayeredWindowBitmap selects it into its own source
  // DC for UpdateLayeredWindow.
  {
    wil::unique_select_object old_bitmap =
        wil::SelectObject(memory_dc.get(), bitmap.get());
    DoPaint(memory_dc.get());
    ::GdiFlush();
  }

  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();
  const int corner_radius = GetRendererWindowCornerRadiusInPixels(
      theme.corner_radius, dpi_, window_size_.cx, window_size_.cy);
  ApplyRoundedRectAlphaAndBorder(pixels, window_size_.cx, window_size_.cy,
                                 corner_radius, /*border_width=*/1,
                                 ToColorRef(theme.border_color));

  return PresentRendererLayeredWindowBitmap(
      m_hWnd, bitmap.get(), window_size_.cx, window_size_.cy,
      RendererWindowOpacityToAlpha(theme.opacity_percent));
}

void RubyWindow::DoPaint(HDC dc) {
  RECT rect = {};
  GetClientRect(&rect);

  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();

  ::SetBkMode(dc, TRANSPARENT);

  HBRUSH bg_brush = ::CreateSolidBrush(ToColorRef(theme.background_color));
  ::FillRect(dc, &rect, bg_brush);
  ::DeleteObject(bg_brush);

  RECT text_rect = rect;
  text_rect.left +=
      GetRubyContentPaddingX(theme, dpi_, vertical_writing_);
  text_rect.top +=
      GetRubyContentPaddingY(theme, dpi_, vertical_writing_);
  text_rect.right -=
      GetRubyContentPaddingX(theme, dpi_, vertical_writing_);
  text_rect.bottom -=
      GetRubyContentPaddingY(theme, dpi_, vertical_writing_);

  if (vertical_writing_) {
    if (text_renderer_ != nullptr &&
        text_renderer_->SupportsVerticalText(TextRenderer::FONTSET_RUBY)) {
      text_renderer_->RenderTextVertical(
          dc, text_,
          Rect(text_rect.left, text_rect.top,
               text_rect.right - text_rect.left,
               text_rect.bottom - text_rect.top),
          TextRenderer::FONTSET_RUBY);
    }
    return;
  }

  HFONT old_font = nullptr;
  if (font_ != nullptr) {
    old_font = static_cast<HFONT>(::SelectObject(dc, font_));
  }

  ::SetTextColor(dc, ToColorRef(theme.text_color));

  ::DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &text_rect,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

  if (old_font != nullptr) {
    ::SelectObject(dc, old_font);
  }
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
