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

#ifndef MOZC_RENDERER_MAC_RUBY_WINDOW_H_
#define MOZC_RENDERER_MAC_RUBY_WINDOW_H_

#include <cstdint>
#include <string>

#include "renderer/mac/RendererBaseWindow.h"

namespace mozc {
namespace commands {
class RendererCommand;
}  // namespace commands

namespace renderer {
namespace mac {

class RubyWindow : public RendererBaseWindow {
 public:
  RubyWindow();
  RubyWindow(const RubyWindow &) = delete;
  RubyWindow &operator=(const RubyWindow &) = delete;
  ~RubyWindow() override;

  bool Update(const commands::RendererCommand &command);

  // Returns the scaled distance between the composition text and ruby window.
  int32_t GetCompositionGap() const;

 private:
  void InitWindow() override;
  void ResetView() override;

  bool BuildReadingText(const commands::RendererCommand &command,
                        std::string *reading) const;

  int32_t composition_gap_ = 4;
};

}  // namespace mac
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_MAC_RUBY_WINDOW_H_