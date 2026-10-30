#include "renderer/win32/conversion_status_window.h"

#include <algorithm>
#include <string>

#include "config/config_handler.h"
#include "base/win32/win_util.h"
#include "renderer/win32/win32_dpi_util.h"

namespace mozc::renderer::win32 {
namespace {
COLORREF RGBColor(uint32_t rgb) {
  return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

// A badge's clip region, painted outline, and shadow must use the same
// physical-pixel radius. Settings are in DIPs, but the window is DPI-aware.
int CornerRadiusPx(
    const config::Config::ConversionStatusIndicatorStyle& style,
    uint32_t dpi, int width, int height) {
  const int maximum = std::min(width, height) / 2;
  if (style.shape() == config::Config::CONVERSION_STATUS_CIRCLE ||
      style.shape() == config::Config::CONVERSION_STATUS_PILL) {
    return maximum;
  }
  return std::clamp(
      ::MulDiv(static_cast<int>(style.corner_radius()),
               static_cast<int>(dpi), 96), 0, maximum);
}

int BorderWidthPx(
    const config::Config::ConversionStatusIndicatorStyle& style,
    uint32_t dpi) {
  const int width = std::clamp(static_cast<int>(style.border_width()), 0, 5);
  return width == 0 ? 0 :
      std::max(1, ::MulDiv(width, static_cast<int>(dpi), 96));
}

// Resolve a UTF-8 family name without depending on Qt in the renderer.
// An invalid or empty value preserves the existing native default.
std::wstring StatusFontFamily(
    const config::Config::ConversionStatusIndicatorStyle& style) {
  const std::string& family = style.font_family();
  if (family.empty() || family.size() > 256) return L"Segoe UI";
  const int length = ::MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, family.data(),
      static_cast<int>(family.size()), nullptr, 0);
  if (length <= 0) return L"Segoe UI";
  std::wstring wide(static_cast<size_t>(length), L'\0');
  if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, family.data(),
                            static_cast<int>(family.size()), wide.data(), length) != length) {
    return L"Segoe UI";
  }
  return wide;
}

std::wstring StatusText(ConversionDisplayStatus state,
    config::Config::ConversionStatusIndicatorLabel label) {
  if (label == config::Config::CONVERSION_STATUS_COLOR_ONLY) return L"";
  if (label == config::Config::CONVERSION_STATUS_SYMBOL) {
    switch (state) {
      case ConversionDisplayStatus::kMozc: return L"M";
      case ConversionDisplayStatus::kZenzPending: return L"…";
      case ConversionDisplayStatus::kZenz: return L"Z";
      case ConversionDisplayStatus::kReading: return L"あ";
      default: return L"";
    }
  }
  switch (state) {
    case ConversionDisplayStatus::kMozc: return L"Mozc";
    case ConversionDisplayStatus::kZenzPending: return L"Zenz…";
    case ConversionDisplayStatus::kZenz: return L"Zenz";
    case ConversionDisplayStatus::kReading: return L"入力中";
    default: return L"";
  }
}

// The saved width is a minimum for text labels; otherwise Zenz… can be
// truncated on non-100% DPI displays. The circle always has fixed diameter.
int RecommendedWidth(HWND hwnd, uint32_t dpi, int height,
                     const config::Config::ConversionStatusIndicatorStyle& style,
                     ConversionDisplayStatus state) {
  const int minimum = std::clamp(::MulDiv(static_cast<int>(style.width()),
                                           static_cast<int>(dpi), 96), 12, 240);
  if (style.shape() == config::Config::CONVERSION_STATUS_CIRCLE) return height;
  const std::wstring label = StatusText(state, style.label());
  if (label.empty()) return minimum;
  HDC dc = ::GetDC(hwnd);
  if (!dc) return minimum;
  const int point_size = std::clamp(static_cast<int>(style.font_size()), 8, 24);
  const std::wstring family = StatusFontFamily(style);
  HFONT font = ::CreateFontW(-::MulDiv(point_size, dpi, 72), 0, 0, 0,
                             FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, family.c_str());
  SIZE measured{};
  HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
  const bool measured_ok = ::GetTextExtentPoint32W(
      dc, label.data(), static_cast<int>(label.size()), &measured) != 0;
  if (previous) ::SelectObject(dc, previous);
  if (font) ::DeleteObject(font);
  ::ReleaseDC(hwnd, dc);
  if (!measured_ok) return minimum;
  const int padding = style.label() == config::Config::CONVERSION_STATUS_FULL_LABEL
                          ? 28 : 16;
  return std::clamp(std::max(minimum, static_cast<int>(measured.cx) +
      ::MulDiv(padding, static_cast<int>(dpi), 96)), 12, 360);
}

// Vertical badges show each Unicode BMP glyph upright from top to bottom.
// All current status labels are BMP; the size is based on glyph line height,
// rather than a horizontal text measurement.
int VerticalTextStep(uint32_t dpi,
    const config::Config::ConversionStatusIndicatorStyle& style) {
  return std::max(1, ::MulDiv(
      std::clamp(static_cast<int>(style.font_size()), 8, 24),
      static_cast<int>(dpi), 72) + ::MulDiv(3, static_cast<int>(dpi), 96));
}

int RecommendedVerticalHeight(uint32_t dpi, int thickness,
    const config::Config::ConversionStatusIndicatorStyle& style,
    ConversionDisplayStatus state) {
  if (style.shape() == config::Config::CONVERSION_STATUS_CIRCLE) {
    return thickness;
  }
  const int minimum = std::clamp(::MulDiv(
      static_cast<int>(style.width()), static_cast<int>(dpi), 96), 12, 240);
  const std::wstring label = StatusText(state, style.label());
  if (label.empty()) return minimum;
  const int dot_padding = style.label() == config::Config::CONVERSION_STATUS_FULL_LABEL
      ? ::MulDiv(18, static_cast<int>(dpi), 96) : 0;
  const int margin = ::MulDiv(12, static_cast<int>(dpi), 96);
  return std::clamp(std::max(minimum,
      static_cast<int>(label.size()) * VerticalTextStep(dpi, style) +
      dot_padding + margin), 12, 360);
}

bool ReadAnchor(const commands::RendererCommand& command,
                const LayoutManager& layout, POINT* position,
                int* line_height) {
  if (command.has_preedit_rectangle()) {
    const auto& box = command.preedit_rectangle();
    if (box.bottom() > box.top()) {
      RECT r{box.left(), box.top(), box.right(), box.bottom()};
      if (command.has_application_info()) {
        const auto& app = command.application_info();
        if (app.input_framework() ==
                commands::RendererCommand::ApplicationInfo::TSF &&
            app.has_target_window_handle() && app.target_window_handle()) {
          // The normal ruby path performs DPI virtualization of these values.
          HWND window = WinUtil::DecodeWindowHandle(
              app.target_window_handle());
          RECT physical = r;
          layout.GetRectInPhysicalCoords(window, r, &physical);
          r = physical;
        }
      }
      position->x = r.left;
      position->y = r.top;
      *line_height = r.bottom - r.top;
      return true;
    }
  }
  if (!command.has_application_info()) return false;
  const auto& app = command.application_info();
  if (!app.has_composition_target() ||
      !app.composition_target().has_top_left()) return false;
  POINT physical{};
  int height = 0;
  if (layout.GetCompositionTargetInPhysicalCoords(app, 22, &physical,
                                                   &height) && height > 0) {
    *position = physical;
    *line_height = height;
    return true;
  }
  return false;
}
}  // namespace

void ConversionStatusWindow::Initialize(HWND owner) {
  if (!IsWindow()) Create(owner);
  ShowWindow(SW_HIDE);
}
void ConversionStatusWindow::Destroy() {
  shadow_.Destroy();
  if (IsWindow()) DestroyWindow();
}
void ConversionStatusWindow::Hide() {
  shadow_.Hide();
  if (IsWindow()) ShowWindow(SW_HIDE);
  status_ = ConversionDisplayStatus::kHidden;
  last_rect_valid_ = false;
}
void ConversionStatusWindow::RaiseWithoutActivation() {
  if (IsWindow() && IsWindowVisible()) {
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}
void ConversionStatusWindow::OnUpdate(
    const commands::RendererCommand& command, const LayoutManager& layout,
    HWND ruby, HWND candidate) {
  const auto settings = config::ConfigHandler::GetSharedConfig();
  // Suppress the badge for active candidate selection, not for an
  // unfocused passive suggestion accompanying live conversion. The latter
  // deliberately coexists with the ruby window.
  const bool candidate_visible = candidate && ::IsWindowVisible(candidate);
  if (!IsWindow() || !command.visible() || !command.has_output() ||
      !settings->show_conversion_status_indicator() ||
      ShouldHideConversionStatusForCandidate(command.output(),
                                             candidate_visible)) {
    Hide();
    return;
  }
  const ConversionDisplayStatus state =
      ResolveConversionDisplayStatus(command.output());
  if (state == ConversionDisplayStatus::kHidden) {
    Hide();
    return;
  }
  const auto& next_style = settings->conversion_status_indicator_style();
  POINT anchor{};
  int line_height = 0;
  if (!ReadAnchor(command, layout, &anchor, &line_height)) {
    Hide();
    return;
  }
  // Check the physical screen's working area, not the primary monitor.
  RECT work{};
  if (!GetWorkingAreaFromPoint(anchor, &work)) {
    Hide();
    return;
  }
  const bool vertical = command.has_application_info() &&
      LayoutManager::GetWritingDirection(command.application_info()) ==
          LayoutManager::VERTICAL_WRITING;
  const uint32_t previous_dpi = dpi_;
  dpi_ = GetDpiForPoint(anchor.x, anchor.y);
  const bool dpi_changed = dpi_ != previous_dpi;
  const int previous_width = width_;
  const int previous_height = height_;
  const bool previous_vertical = vertical_;
  const int thickness = std::clamp(::MulDiv(
      static_cast<int>(next_style.height()), static_cast<int>(dpi_), 96), 12, 100);
  if (vertical) {
    width_ = thickness;
    height_ = RecommendedVerticalHeight(dpi_, thickness, next_style, state);
  } else {
    height_ = thickness;
    width_ = RecommendedWidth(m_hWnd, dpi_, height_, next_style, state);
  }
  const Rect preedit = vertical
      ? Rect(anchor.x - line_height, anchor.y, line_height, line_height)
      : Rect(anchor.x, anchor.y, line_height, line_height);
  auto as_rect = [](const RECT& r) {
    return Rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
  };
  RECT wr{};
  Rect ruby_rect, obstacle_rect;
  const Rect* pruby = nullptr;
  const Rect* pobstacle = nullptr;
  if (ruby && ::IsWindowVisible(ruby) && ::GetWindowRect(ruby, &wr)) {
    ruby_rect = as_rect(wr);
    pruby = &ruby_rect;
  }
  if (candidate && ::IsWindowVisible(candidate) &&
      ::GetWindowRect(candidate, &wr)) {
    obstacle_rect = as_rect(wr);
    pobstacle = &obstacle_rect;
  }
  Rect rect;
  if (!PlaceConversionStatusIndicator(
          preedit, pruby, pobstacle, as_rect(work),
          Size(width_, height_), vertical, next_style, &rect)) {
    Hide();
    return;
  }
  const bool repaint = status_ != state ||
      style_.SerializeAsString() != next_style.SerializeAsString();
  const bool was_visible = IsWindowVisible();
  status_ = state;
  style_.CopyFrom(next_style);
  vertical_ = vertical;
  const int radius = CornerRadiusPx(style_, dpi_, width_, height_);
  if (repaint || width_ != previous_width || height_ != previous_height ||
      vertical != previous_vertical || dpi_changed) {
    const int diameter = std::max(1, radius * 2);
    HRGN region = CreateRoundRectRgn(0, 0, width_, height_,
                                     diameter, diameter);
    if (region) SetWindowRgn(region, FALSE);  // Windows owns the region.
  }
  SetLayeredWindowAttributes(m_hWnd, 0, static_cast<BYTE>(
      std::clamp(style_.opacity_percent(), 20u, 100u) * 255 / 100), LWA_ALPHA);
  const bool dimensions_changed = width_ != previous_width ||
                                  height_ != previous_height;
  const bool position_changed = !last_rect_valid_ ||
      rect.Left() != last_rect_.left || rect.Top() != last_rect_.top;
  if (position_changed || dimensions_changed || !was_visible) {
    SetWindowPos(HWND_TOPMOST, rect.Left(), rect.Top(), width_, height_,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  if (repaint || dimensions_changed || vertical != previous_vertical ||
      dpi_changed || !was_visible) InvalidateRect(nullptr, TRUE);
  last_rect_ = RECT{rect.Left(), rect.Top(), rect.Right(), rect.Bottom()};
  last_rect_valid_ = true;
  RECT actual{};
  ::GetWindowRect(m_hWnd, &actual);
  RendererWindowShadowStyle shadow_style;
  shadow_style.size = std::clamp(style_.shadow_size(), 0u, 24u);
  shadow_style.opacity_percent = std::clamp(
      style_.shadow_opacity_percent(), 0u, 100u);
  shadow_style.angle_degrees = style_.shadow_angle_degrees() % 360;
  shadow_style.distance = std::clamp(style_.shadow_distance(), 0u, 24u);
  if (repaint || dimensions_changed || position_changed ||
      dpi_changed || !was_visible) {
    shadow_.Update(m_hWnd, actual, dpi_, radius, shadow_style);
  }
}
LRESULT ConversionStatusWindow::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
  return 1;
}
LRESULT ConversionStatusWindow::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
  PAINTSTRUCT ps{};
  HDC dc = BeginPaint(&ps);
  RECT client{};
  GetClientRect(&client);
  HBRUSH brush = CreateSolidBrush(
      RGBColor(ConversionStatusColor(status_, style_)));
  const int radius = CornerRadiusPx(style_, dpi_, width_, height_);
  const int stroke = BorderWidthPx(style_, dpi_);
  HPEN pen = CreatePen(stroke ? PS_SOLID : PS_NULL,
                      std::max(1, stroke), RGBColor(style_.border_color()));
  // Fill all pixels permitted by the window region before drawing the
  // rounded shape. The region and GDI RoundRect otherwise disagree on a
  // few boundary pixels, exposing the window's default (often white) fill.
  FillRect(dc, &client, brush);
  HGDIOBJ old_brush = SelectObject(dc, brush);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  RoundRect(dc, 0, 0, width_, height_,
            std::max(1, radius * 2), std::max(1, radius * 2));
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);
  // Subtle top-edge highlight; no extra bitmap allocation or alpha surface.
  if (!vertical_ && style_.border_width() == 0 && height_ >= 18 &&
      style_.shape() != config::Config::CONVERSION_STATUS_CIRCLE) {
    const uint32_t rgb = ConversionStatusColor(status_, style_);
    const auto channel = [rgb](int shift) {
      return std::min(255, static_cast<int>((rgb >> shift) & 255) + 22);
    };
    HPEN line = CreatePen(PS_SOLID, 1,
                          RGB(channel(16), channel(8), channel(0)));
    HGDIOBJ prior = SelectObject(dc, line);
    MoveToEx(dc, std::min(width_ - 3, height_ / 2), 1, nullptr);
    LineTo(dc, std::max(2, width_ - height_ / 2), 1);
    SelectObject(dc, prior);
    DeleteObject(line);
  }
  const auto label = style_.shape() == config::Config::CONVERSION_STATUS_CIRCLE &&
                     style_.label() == config::Config::CONVERSION_STATUS_FULL_LABEL
      ? config::Config::CONVERSION_STATUS_SYMBOL : style_.label();
  const std::wstring text = StatusText(status_, label);
  if (!text.empty()) {
    const int point_size = std::clamp(static_cast<int>(style_.font_size()), 8, 24);
    const std::wstring family = StatusFontFamily(style_);
    HFONT font = CreateFontW(-::MulDiv(point_size, dpi_, 72), 0, 0, 0,
                            FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, family.c_str());
    HGDIOBJ previous_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGBColor(style_.text_color()));
    RECT label_rect = client;
    const bool show_dot = style_.shape() != config::Config::CONVERSION_STATUS_CIRCLE &&
                          label == config::Config::CONVERSION_STATUS_FULL_LABEL;
    if (vertical_ && style_.shape() != config::Config::CONVERSION_STATUS_CIRCLE) {
      const int step = VerticalTextStep(dpi_, style_);
      const int dot_gap = show_dot ? ::MulDiv(18, static_cast<int>(dpi_), 96) : 0;
      const int text_height = static_cast<int>(text.size()) * step;
      const int top = std::max(0, (height_ - text_height - dot_gap) / 2);
      if (show_dot) {
        const int dot_r = std::max(2, ::MulDiv(3, static_cast<int>(dpi_), 96));
        const int dot_y = top + dot_gap / 2;
        HBRUSH dot_brush = CreateSolidBrush(RGBColor(style_.text_color()));
        HGDIOBJ saved_brush = SelectObject(dc, dot_brush);
        HGDIOBJ saved_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, width_ / 2 - dot_r, dot_y - dot_r,
                width_ / 2 + dot_r, dot_y + dot_r);
        SelectObject(dc, saved_pen);
        SelectObject(dc, saved_brush);
        DeleteObject(dot_brush);
      }
      for (size_t i = 0; i < text.size(); ++i) {
        const int y = top + dot_gap + static_cast<int>(i) * step;
        RECT glyph_rect{0, y, width_, y + step};
        DrawTextW(dc, text.c_str() + i, 1, &glyph_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }
    } else {
      if (show_dot) {
        const int dot_x = ::MulDiv(12, static_cast<int>(dpi_), 96);
        const int dot_r = std::max(2, ::MulDiv(3, static_cast<int>(dpi_), 96));
        HBRUSH dot_brush = CreateSolidBrush(RGBColor(style_.text_color()));
        HGDIOBJ saved_brush = SelectObject(dc, dot_brush);
        HGDIOBJ saved_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, dot_x - dot_r, height_ / 2 - dot_r,
                dot_x + dot_r, height_ / 2 + dot_r);
        SelectObject(dc, saved_pen);
        SelectObject(dc, saved_brush);
        DeleteObject(dot_brush);
        label_rect.left += ::MulDiv(18, static_cast<int>(dpi_), 96);
        label_rect.right -= ::MulDiv(4, static_cast<int>(dpi_), 96);
      }
      DrawTextW(dc, text.c_str(), -1, &label_rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, previous_font);
    DeleteObject(font);
  }
  EndPaint(&ps);
  return 0;
}
}  // namespace mozc::renderer::win32
