#ifndef MOZC_RENDERER_WIN32_RUBY_WINDOW_H_
#define MOZC_RENDERER_WIN32_RUBY_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>

#include "protocol/renderer_command.pb.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_renderer_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

typedef ATL::CWinTraits<WS_POPUP,
                        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                            WS_EX_NOACTIVATE>
    RubyWindowTraits;

class RubyWindow
    : public ATL::CWindowImpl<RubyWindow, ATL::CWindow, RubyWindowTraits> {
 public:
  DECLARE_WND_CLASS_EX(L"MozcRubyWindow", CS_SAVEBITS, COLOR_WINDOW);

  RubyWindow();
  RubyWindow(const RubyWindow&) = delete;
  RubyWindow& operator=(const RubyWindow&) = delete;
  ~RubyWindow();

  BEGIN_MSG_MAP(RubyWindow)
    MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
    MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
    MESSAGE_HANDLER(WM_PAINT, OnPaint)
  END_MSG_MAP()

  void Initialize();
  void Destroy();
  void Hide();
  // Reassert the ruby body at the top of the topmost band without moving or
  // activating it. WindowManager calls this after candidate-family bodies are
  // positioned so all renderer bodies remain above their custom shadows.
  void RaiseToTopmostWithoutActivation();
  void OnUpdate(const commands::RendererCommand& command,
                const LayoutManager& layout_manager,
                const RECT* avoid_rect = nullptr);

 private:
  LRESULT OnEraseBkgnd(UINT msg_id, WPARAM wparam, LPARAM lparam,
                       BOOL& handled);
  LRESULT OnShowWindow(UINT msg_id, WPARAM wparam, LPARAM lparam,
                       BOOL& handled);
  LRESULT OnPaint(UINT msg_id, WPARAM wparam, LPARAM lparam, BOOL& handled);

  void DoPaint(HDC dc);
  bool RenderAndPresent();
  void ResetFont();
  void UpdateDpi(uint32_t dpi);
  void UpdateFont();
  SIZE MeasureText(bool vertical_writing) const;
  bool BuildReadingText(const commands::RendererCommand& command,
                        std::string* reading) const;
  struct TargetIdentity {
    uint32_t process_id = 0;
    uint32_t thread_id = 0;
    uint32_t target_window_handle = 0;
  };

  static bool GetTargetIdentity(const commands::RendererCommand& command,
                                TargetIdentity* identity);
  static bool IsSameTargetIdentity(const TargetIdentity& lhs,
                                   const TargetIdentity& rhs);

  void HideWindowOnly();
  void ClearPlacementTracking();
  bool KeepCurrentPlacement() const;
  bool ShouldRejectTransientGeometry(const POINT& base_point, int line_height,
                                     const RECT& window_rect,
                                     const RECT& work_area,
                                     bool from_preedit_rectangle,
                                     bool target_changed) const;
  bool GetBasePosition(const commands::RendererCommand& command,
                       const LayoutManager& layout_manager,
                       bool vertical_writing, POINT* point, int* line_height,
                       bool* from_preedit_rectangle) const;

  HFONT font_ = nullptr;
  std::wstring font_face_name_;
  int font_height_ = 0;
  int font_weight_ = 0;
  uint32_t ruby_text_color_ = 0xffffffff;
  uint32_t dpi_ = USER_DEFAULT_SCREEN_DPI;
  std::unique_ptr<TextRenderer> text_renderer_;

  std::wstring text_;
  bool vertical_writing_ = false;
  SIZE window_size_ = {};
  RendererShadowWindow shadow_window_;

  TargetIdentity last_target_identity_;
  bool has_last_target_identity_ = false;
  RECT last_valid_window_rect_ = {};

  bool has_last_valid_geometry_ = false;
  int transient_geometry_reject_count_ = 0;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_RUBY_WINDOW_H_
