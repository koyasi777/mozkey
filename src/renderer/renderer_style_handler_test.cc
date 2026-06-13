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

#include "protocol/renderer_style.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {

TEST(RendererStyleHandlerTest, GetRendererStyle) {
  RendererStyle style;
  RendererStyleHandler::GetRendererStyle(&style);
  EXPECT_TRUE(style.has_window_border());
  EXPECT_TRUE(style.has_infolist_style());
  EXPECT_TRUE(style.infolist_style().has_focused_border_color());
}

TEST(RendererStyleHandlerTest, ApplyCandidateRubyFont) {
  RendererStyle style;
  RendererStyleHandler::GetDefaultRendererStyle(&style);

  RendererStyleHandler::ApplyCandidateRubyFont("Yu Gothic UI", &style);

  EXPECT_FALSE(style.shortcut_style().has_font_name());
  EXPECT_EQ("Yu Gothic UI", style.candidate_style().font_name());
  EXPECT_EQ("Yu Gothic UI", style.description_style().font_name());
  EXPECT_FALSE(style.footer_style().has_font_name());
  EXPECT_FALSE(style.footer_sub_label_style().has_font_name());
  EXPECT_EQ("Yu Gothic UI",
            style.infolist_style().caption_style().font_name());
  EXPECT_EQ("Yu Gothic UI", style.infolist_style().title_style().font_name());
  EXPECT_EQ("Yu Gothic UI",
            style.infolist_style().description_style().font_name());
}

TEST(RendererStyleHandlerTest, ApplyCandidateRubyFontSkipsEmptyFontName) {
  RendererStyle style;
  RendererStyleHandler::GetDefaultRendererStyle(&style);

  RendererStyleHandler::ApplyCandidateRubyFont("", &style);

  EXPECT_FALSE(style.candidate_style().has_font_name());
  EXPECT_FALSE(style.footer_style().has_font_name());
  EXPECT_FALSE(style.infolist_style().title_style().has_font_name());
}

}  // namespace renderer
}  // namespace mozc
