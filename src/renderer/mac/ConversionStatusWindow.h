#ifndef MOZC_RENDERER_MAC_CONVERSION_STATUS_WINDOW_H_
#define MOZC_RENDERER_MAC_CONVERSION_STATUS_WINDOW_H_

#include "protocol/renderer_command.pb.h"
#include "renderer/conversion_status_indicator.h"
#include "renderer/mac/RendererBaseWindow.h"

namespace mozc::renderer::mac {
class ConversionStatusWindow : public RendererBaseWindow {
 public:
  ConversionStatusWindow() = default;
  ~ConversionStatusWindow() override = default;
  bool Update(const commands::RendererCommand& command, bool vertical);

 private:
  void ResetView() override;
  ConversionDisplayStatus last_status_ = ConversionDisplayStatus::kHidden;
  config::Config::ConversionStatusIndicatorStyle last_style_;
  bool last_vertical_ = false;
};
}  // namespace mozc::renderer::mac
#endif
