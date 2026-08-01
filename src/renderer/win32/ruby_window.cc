#include "renderer/win32/ruby_window.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "base/win32/wide_char.h"
#include "config/config_handler.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/renderer_style_handler.h"
#include "renderer/win32/win32_dpi_util.h"
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

int ScaleCornerRadius(uint32_t logical_radius, uint32_t dpi) {
  if (logical_radius == 0) {
    return 0;
  }
  return static_cast<int>(
      std::lround(logical_radius * GetDPIScalingFactor(dpi)));
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

int GetRubyFontPointSize() {
  return ScaleByPercent(
      kFontPointSize, RendererStyleHandler::GetRubyWindowStyle().size_percent);
}

bool IsLiveConversionRubyWindowEnabled() {
  const auto config = config::ConfigHandler::GetSharedConfig();
  return config == nullptr || config->show_live_conversion_ruby_window();
}

std::wstring GetRubyWindowFontFaceName() {
  RendererStyle style;
  if (!RendererStyleHandler::GetRendererStyle(&style)) {
    return L"Yu Gothic UI";
  }

  const RendererStyle::TextStyle& candidate_style = style.candidate_style();
  if (!candidate_style.has_font_name() || candidate_style.font_name().empty()) {
    return L"Yu Gothic UI";
  }

  const std::wstring font_name =
      mozc::win32::Utf8ToWide(candidate_style.font_name());
  if (font_name.empty()) {
    return L"Yu Gothic UI";
  }

  if (font_name.front() == L'@' || font_name.size() >= LF_FACESIZE) {
    return L"Yu Gothic UI";
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

  ApplyRendererWindowOpacity(
      m_hWnd, RendererStyleHandler::GetRubyWindowStyle().opacity_percent);

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
                                 POINT* point, int* line_height,
                                 bool* from_preedit_rectangle) const {
  // Prefer the preedit rectangle. This is usually closer to the actual
  // composition text than composition_target, which tends to follow the caret.
  if (command.has_preedit_rectangle()) {
    const commands::RendererCommand::Rectangle& rect =
        command.preedit_rectangle();

    const int height = rect.bottom() - rect.top();
    if (height > 0) {
      point->x = rect.left();
      point->y = rect.top();
      *line_height = height;
      if (from_preedit_rectangle != nullptr) {
        *from_preedit_rectangle = true;
      }
      return true;
    }
  }

  // Fallback for clients that do not provide a usable preedit rectangle.
  if (command.has_application_info()) {
    const commands::RendererCommand::ApplicationInfo& app_info =
        command.application_info();

    if (app_info.has_composition_target() &&
        app_info.composition_target().has_top_left()) {
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
    }
  }

  return false;
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
}

void RubyWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == 0 || dpi == dpi_) {
    return;
  }
  dpi_ = dpi;
  ResetFont();
}

void RubyWindow::UpdateFont() {
  const std::wstring font_name = GetRubyWindowFontFaceName();
  const int font_height = -MulDiv(GetRubyFontPointSize(), dpi_, 72);
  const int font_weight = FW_SEMIBOLD;

  if (font_ != nullptr &&
      font_height_ == font_height &&
      font_weight_ == font_weight &&
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
  if (font_ == nullptr && font_name != L"Yu Gothic UI") {
    wcscpy_s(logfont.lfFaceName, L"Yu Gothic UI");
    font_ = ::CreateFontIndirectW(&logfont);
    actual_font_name = L"Yu Gothic UI";
  }

  if (font_ == nullptr) {
    return;
  }

  font_face_name_ = actual_font_name;
  font_height_ = font_height;
  font_weight_ = font_weight;
}

SIZE RubyWindow::MeasureText() const {
  SIZE size = {};
  if (text_.empty()) {
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

  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();
  size.cx += GetRubyPaddingX(theme, dpi_) * 2;
  size.cy += GetRubyPaddingY(theme, dpi_) * 2;
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
  if (!GetBasePosition(command, layout_manager, &base_point, &line_height,
                       &from_preedit_rectangle)) {
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
  const auto restore_previous_content = [&]() {
    text_ = previous_text;
    window_size_ = previous_window_size;
  };

  text_ = ToWide(reading);
  window_size_ = MeasureText();
  if (window_size_.cx <= 0 || window_size_.cy <= 0) {
    restore_previous_content();
    Hide();
    return;
  }

  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();
  const int gap = ScaleRubySpacing(
      static_cast<int>(theme.composition_gap), theme.size_percent, dpi_);

  const RECT work_area = GetWorkAreaForPoint(base_point);

  // Keep the left edge stable while the reading text grows. Centering a ruby
  // chip that is wider than the preedit makes the window expand both left and
  // right on every keystroke, which is visually noisy in live conversion.
  const int preedit_width = GetPreeditWidth(command);

  int left = base_point.x;
  if (preedit_width > window_size_.cx) {
    left = base_point.x + (preedit_width - window_size_.cx) / 2;
  }
  left = ClampInt(left, work_area.left, work_area.right - window_size_.cx);

  const int above_top = base_point.y - window_size_.cy - gap;
  const int below_top = base_point.y + line_height + gap;

  const auto ruby_rect_at = [&](int top) {
    return RECT{left, top, left + window_size_.cx, top + window_size_.cy};
  };

  const auto try_place = [&](int top, int* chosen_top) {
    const RECT rect = ruby_rect_at(top);
    if (!IsUsableRubyRect(rect, work_area, avoid_rect)) {
      return false;
    }
    *chosen_top = top;
    return true;
  };

  int top = 0;
  // Prefer the conventional ruby-like placement above the preedit.  Use below
  // only when the above side is unavailable or occupied by candidate/suggestion
  // UI.  If neither side is usable, keep the last stable placement when this
  // looks like a transient geometry frame.
  const RECT preferred_window_rect = ruby_rect_at(above_top);
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

  RECT window_rect = {left, top, left + window_size_.cx,
                      top + window_size_.cy};
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

  const int radius = ScaleCornerRadius(
      RendererStyleHandler::GetRubyWindowStyle().corner_radius, dpi_);
  if (radius <= 0) {
    ::SetWindowRgn(m_hWnd, nullptr, TRUE);
  } else {
    HRGN region = ::CreateRoundRectRgn(0, 0, window_size_.cx + 1,
                                       window_size_.cy + 1, radius * 2,
                                       radius * 2);
    ::SetWindowRgn(m_hWnd, region, TRUE);
    // Ownership of |region| is transferred to the window.
  }

  SetWindowPos(HWND_TOPMOST, left, top, window_size_.cx, window_size_.cy,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ApplyRendererWindowOpacity(m_hWnd, GetRubyWindowTheme().opacity_percent);
  RendererWindowShadowStyle shadow_style;
  shadow_style.size = GetRubyWindowTheme().shadow.size;
  shadow_style.opacity_percent = GetRubyWindowTheme().shadow.opacity_percent;
  shadow_style.angle_degrees = GetRubyWindowTheme().shadow.angle_degrees;
  shadow_style.distance = GetRubyWindowTheme().shadow.distance;
  shadow_window_.Update(m_hWnd, window_rect, dpi_,
                        GetRubyWindowTheme().corner_radius, shadow_style);

  last_valid_window_rect_ = window_rect;

  has_last_valid_geometry_ = true;
  if (has_current_target_identity) {
    last_target_identity_ = current_target_identity;
    has_last_target_identity_ = true;
  }

  ShowWindow(SW_SHOWNOACTIVATE);
  Invalidate(FALSE);
}

LRESULT RubyWindow::OnEraseBkgnd(UINT msg_id,
                                 WPARAM wparam,
                                 LPARAM lparam,
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

LRESULT RubyWindow::OnPaint(UINT msg_id,
                            WPARAM wparam,
                            LPARAM lparam,
                            BOOL& handled) {
  PAINTSTRUCT ps = {};
  HDC dc = BeginPaint(&ps);
  DoPaint(dc);
  EndPaint(&ps);
  return 0;
}

void RubyWindow::DoPaint(HDC dc) {
  RECT rect = {};
  GetClientRect(&rect);

  const RendererStyleHandler::RubyWindowStyle theme = GetRubyWindowTheme();

  ::SetBkMode(dc, TRANSPARENT);

  HBRUSH bg_brush = ::CreateSolidBrush(ToColorRef(theme.background_color));
  HPEN border_pen = ::CreatePen(PS_SOLID, 1, ToColorRef(theme.border_color));

  HGDIOBJ old_brush = ::SelectObject(dc, bg_brush);
  HGDIOBJ old_pen = ::SelectObject(dc, border_pen);

  const int radius = ScaleCornerRadius(theme.corner_radius, dpi_);
  if (radius <= 0) {
    ::Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
  } else {
    ::RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2,
                radius * 2);
  }

  ::SelectObject(dc, old_pen);
  ::SelectObject(dc, old_brush);
  ::DeleteObject(border_pen);
  ::DeleteObject(bg_brush);

  HFONT old_font = nullptr;
  if (font_ != nullptr) {
    old_font = static_cast<HFONT>(::SelectObject(dc, font_));
  }

  ::SetTextColor(dc, ToColorRef(theme.text_color));

  RECT text_rect = rect;
  text_rect.left += GetRubyPaddingX(theme, dpi_);
  text_rect.top += GetRubyPaddingY(theme, dpi_);
  text_rect.right -= GetRubyPaddingX(theme, dpi_);
  text_rect.bottom -= GetRubyPaddingY(theme, dpi_);

  ::DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &text_rect,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

  if (old_font != nullptr) {
    ::SelectObject(dc, old_font);
  }
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
