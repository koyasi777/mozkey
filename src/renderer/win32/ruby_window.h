#ifndef MOZC_RENDERER_WIN32_RUBY_WINDOW_H_
#define MOZC_RENDERER_WIN32_RUBY_WINDOW_H_

#include <windows.h>

#include <string>

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>

#include "protocol/renderer_command.pb.h"

namespace mozc {
namespace renderer {
namespace win32 {

typedef ATL::CWinTraits<WS_POPUP,
                        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE>
    RubyWindowTraits;

class RubyWindow
    : public ATL::CWindowImpl<RubyWindow, ATL::CWindow, RubyWindowTraits> {
 public:
  DECLARE_WND_CLASS_EX(L"MozcRubyWindow", CS_SAVEBITS | CS_DROPSHADOW,
                       COLOR_WINDOW);

  RubyWindow();
  RubyWindow(const RubyWindow&) = delete;
  RubyWindow& operator=(const RubyWindow&) = delete;
  ~RubyWindow();

  BEGIN_MSG_MAP(RubyWindow)
    MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
    MESSAGE_HANDLER(WM_PAINT, OnPaint)
  END_MSG_MAP()

  void Initialize();
  void Destroy();
  void Hide();
  void OnUpdate(const commands::RendererCommand& command);

 private:
  LRESULT OnEraseBkgnd(UINT msg_id, WPARAM wparam, LPARAM lparam,
                       BOOL& handled);
  LRESULT OnPaint(UINT msg_id, WPARAM wparam, LPARAM lparam, BOOL& handled);

  void DoPaint(HDC dc);
  void ResetFont();
  void UpdateFont(HDC dc);
  SIZE MeasureText() const;
  bool BuildReadingText(const commands::RendererCommand& command,
                        std::string* reading) const;
  bool GetBasePosition(const commands::RendererCommand& command,
                       POINT* point, int* line_height) const;

  HFONT font_ = nullptr;
  std::wstring font_face_name_;
  int font_height_ = 0;
  int font_weight_ = 0;
  int font_dpi_y_ = 0;

  std::wstring text_;
  SIZE window_size_ = {};
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_RUBY_WINDOW_H_
