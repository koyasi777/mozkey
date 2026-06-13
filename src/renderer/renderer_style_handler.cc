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

#include "renderer/renderer_style_handler.h"

#include <string>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "base/protobuf/text_format.h"
#include "base/singleton.h"
#include "protocol/renderer_style.pb.h"

namespace mozc {
namespace renderer {
namespace {

// absl::string_view kStyleTextProto is defined in renderer_style.inc.
#include "renderer/renderer_style.inc"

void SetColor(RendererStyle::RGBAColor* color, int r, int g, int b) {
  color->set_r(r);
  color->set_g(g);
  color->set_b(b);
}

void ApplyLightCandidateWindowTheme(RendererStyle* style) {
  SetColor(style->mutable_border_color(), 0x96, 0x96, 0x96);

  SetColor(style->mutable_shortcut_style()->mutable_foreground_color(),
           0x77, 0x77, 0x77);
  SetColor(style->mutable_shortcut_style()->mutable_background_color(),
           0xf3, 0xf4, 0xff);

  SetColor(style->mutable_candidate_style()->mutable_foreground_color(),
           0x00, 0x00, 0x00);
  SetColor(style->mutable_candidate_style()->mutable_background_color(),
           0xff, 0xff, 0xff);

  SetColor(style->mutable_description_style()->mutable_foreground_color(),
           0x88, 0x88, 0x88);
  SetColor(style->mutable_description_style()->mutable_background_color(),
           0xff, 0xff, 0xff);

  SetColor(style->mutable_footer_style()->mutable_foreground_color(),
           0x4c, 0x4c, 0x4c);
  SetColor(style->mutable_footer_sub_label_style()->mutable_foreground_color(),
           0xa7, 0xa7, 0xa7);

  if (style->footer_border_colors_size() == 0) {
    style->add_footer_border_colors();
  }
  SetColor(style->mutable_footer_border_colors(0), 0x60, 0x60, 0x60);

  SetColor(style->mutable_footer_top_color(), 0xff, 0xff, 0xff);
  SetColor(style->mutable_footer_bottom_color(), 0xee, 0xee, 0xee);

  SetColor(style->mutable_focused_background_color(), 0xd1, 0xea, 0xff);
  SetColor(style->mutable_focused_border_color(), 0x7f, 0xac, 0xdd);

  SetColor(style->mutable_scrollbar_background_color(), 0xe0, 0xe0, 0xe0);
  SetColor(style->mutable_scrollbar_indicator_color(), 0x75, 0x90, 0xb8);

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  SetColor(infostyle->mutable_caption_style()->mutable_foreground_color(),
           0x00, 0x00, 0x00);
  SetColor(infostyle->mutable_title_style()->mutable_foreground_color(),
           0x00, 0x00, 0x00);
  SetColor(infostyle->mutable_title_style()->mutable_background_color(),
           0xff, 0xff, 0xff);
  SetColor(infostyle->mutable_description_style()->mutable_foreground_color(),
           0x33, 0x33, 0x33);
  SetColor(infostyle->mutable_description_style()->mutable_background_color(),
           0xff, 0xff, 0xff);
  SetColor(infostyle->mutable_border_color(), 0x96, 0x96, 0x96);
  SetColor(infostyle->mutable_caption_background_color(), 0xec, 0xf0, 0xfa);
  SetColor(infostyle->mutable_focused_background_color(), 0xd1, 0xea, 0xff);
  SetColor(infostyle->mutable_focused_border_color(), 0x7f, 0xac, 0xdd);
}

void ApplyDarkCandidateWindowTheme(RendererStyle* style) {
  SetColor(style->mutable_border_color(), 0x32, 0x38, 0x40);

  SetColor(style->mutable_shortcut_style()->mutable_foreground_color(),
           0x96, 0xa0, 0xaa);
  SetColor(style->mutable_shortcut_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);

  SetColor(style->mutable_candidate_style()->mutable_foreground_color(),
           0xe6, 0xed, 0xf3);
  SetColor(style->mutable_candidate_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);

  SetColor(style->mutable_description_style()->mutable_foreground_color(),
           0x8b, 0x94, 0x9e);
  SetColor(style->mutable_description_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);

  SetColor(style->mutable_footer_style()->mutable_foreground_color(),
           0xb7, 0xc0, 0xc9);
  SetColor(style->mutable_footer_sub_label_style()->mutable_foreground_color(),
           0x77, 0x80, 0x8a);

  if (style->footer_border_colors_size() == 0) {
    style->add_footer_border_colors();
  }
  SetColor(style->mutable_footer_border_colors(0), 0x2a, 0x30, 0x37);

  SetColor(style->mutable_footer_top_color(), 0x16, 0x1a, 0x1f);
  SetColor(style->mutable_footer_bottom_color(), 0x16, 0x1a, 0x1f);

  SetColor(style->mutable_focused_background_color(), 0x24, 0x2b, 0x34);
  SetColor(style->mutable_focused_border_color(), 0x3f, 0x4b, 0x59);

  SetColor(style->mutable_scrollbar_background_color(), 0x1d, 0x22, 0x28);
  SetColor(style->mutable_scrollbar_indicator_color(), 0x4b, 0x57, 0x66);

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  SetColor(infostyle->mutable_caption_style()->mutable_foreground_color(),
           0xe6, 0xed, 0xf3);
  SetColor(infostyle->mutable_title_style()->mutable_foreground_color(),
           0xe6, 0xed, 0xf3);
  SetColor(infostyle->mutable_title_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);
  SetColor(infostyle->mutable_description_style()->mutable_foreground_color(),
           0xc9, 0xd1, 0xd9);
  SetColor(infostyle->mutable_description_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);
  SetColor(infostyle->mutable_border_color(), 0x32, 0x38, 0x40);
  SetColor(infostyle->mutable_caption_background_color(), 0x1a, 0x1e, 0x24);
  SetColor(infostyle->mutable_focused_background_color(), 0x24, 0x2b, 0x34);
  SetColor(infostyle->mutable_focused_border_color(), 0x3f, 0x4b, 0x59);
}

class RendererStyleHandlerImpl {
 public:
  RendererStyleHandlerImpl();
  ~RendererStyleHandlerImpl() = default;

  bool GetRendererStyle(RendererStyle* style);
  bool SetRendererStyle(const RendererStyle& style);
  void GetDefaultRendererStyle(RendererStyle* style);

 private:
  RendererStyle style_;
};

RendererStyleHandlerImpl* GetRendererStyleHandlerImpl() {
  return Singleton<RendererStyleHandlerImpl>::get();
}

RendererStyleHandlerImpl::RendererStyleHandlerImpl() {
  GetDefaultRendererStyle(&style_);
}

bool RendererStyleHandlerImpl::GetRendererStyle(RendererStyle* style) {
  if (style == nullptr) {
    return false;
  }
  *style = style_;
  return true;
}

bool RendererStyleHandlerImpl::SetRendererStyle(const RendererStyle& style) {
  style_ = style;
  return true;
}

void RendererStyleHandlerImpl::GetDefaultRendererStyle(RendererStyle* style) {
  CHECK(style != nullptr);
  style->Clear();
  CHECK(mozc::protobuf::TextFormat::ParseFromString(kStyleTextProto, style));
  ApplyLightCandidateWindowTheme(style);
}

}  // namespace

bool RendererStyleHandler::GetRendererStyle(RendererStyle* style) {
  return GetRendererStyleHandlerImpl()->GetRendererStyle(style);
}

bool RendererStyleHandler::SetRendererStyle(const RendererStyle& style) {
  return GetRendererStyleHandlerImpl()->SetRendererStyle(style);
}

void RendererStyleHandler::GetDefaultRendererStyle(RendererStyle* style) {
  return GetRendererStyleHandlerImpl()->GetDefaultRendererStyle(style);
}

void RendererStyleHandler::ApplyCandidateWindowTheme(bool use_dark_mode,
                                                     RendererStyle* style) {
  if (style == nullptr) {
    return;
  }
  if (use_dark_mode) {
    ApplyDarkCandidateWindowTheme(style);
  } else {
    ApplyLightCandidateWindowTheme(style);
  }
}

void RendererStyleHandler::ApplyCandidateRubyFont(
    const std::string& font_name, RendererStyle* style) {
  if (style == nullptr || font_name.empty()) {
    return;
  }

  style->mutable_candidate_style()->set_font_name(font_name);
  style->mutable_description_style()->set_font_name(font_name);

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  infostyle->mutable_caption_style()->set_font_name(font_name);
  infostyle->mutable_title_style()->set_font_name(font_name);
  infostyle->mutable_description_style()->set_font_name(font_name);
}

}  // namespace renderer
}  // namespace mozc
