#ifndef MOZC_RENDERER_WIN32_CONVERSION_STATUS_WINDOW_H_
#define MOZC_RENDERER_WIN32_CONVERSION_STATUS_WINDOW_H_

#include <windows.h>
#include <atlbase.h>
#include <atlwin.h>
#include <cstdint>

#include "renderer/conversion_status_indicator.h"
#include "renderer/win32/win32_renderer_util.h"

namespace mozc::renderer::win32 {

using ConversionStatusWindowTraits = ATL::CWinTraits<
    WS_POPUP | WS_DISABLED,
    WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
        WS_EX_NOACTIVATE | WS_EX_TRANSPARENT>;

class ConversionStatusWindow : public ATL::CWindowImpl<
    ConversionStatusWindow, ATL::CWindow, ConversionStatusWindowTraits> {
 public:
  DECLARE_WND_CLASS_EX(L"MozkeyConversionStatusWindow", CS_SAVEBITS, COLOR_WINDOW);
  BEGIN_MSG_MAP(ConversionStatusWindow)
    MESSAGE_HANDLER(WM_PAINT, OnPaint)
    MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
  END_MSG_MAP()

  void Initialize(HWND owner);
  void Destroy();
  void Hide();
  void OnUpdate(const commands::RendererCommand& command,
                const LayoutManager& layout, HWND ruby, HWND candidate);
  void RaiseWithoutActivation();

 private:
  LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
  RendererShadowWindow shadow_;
  ConversionDisplayStatus status_ = ConversionDisplayStatus::kHidden;
  config::Config::ConversionStatusIndicatorStyle style_;
  uint32_t dpi_ = 96;
  int width_ = 0;
  int height_ = 0;
  bool vertical_ = false;
  RECT last_rect_{};
  bool last_rect_valid_ = false;
};

}  // namespace mozc::renderer::win32
#endif  // MOZC_RENDERER_WIN32_CONVERSION_STATUS_WINDOW_H_
