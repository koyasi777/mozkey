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

#import <Cocoa/Cocoa.h>

#include <cstddef>

#include "base/coordinates.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/mac/mac_writing_direction.h"

namespace mozc {
namespace client {
class SendCommandInterface;
}  // namespace mozc::client

namespace renderer {
class TableLayout;
class RendererStyle;

// Detailed usage type for each column in the macOS candidate table view.
enum ColumnType {
  kColumnShortcut = 0,  // Show candidate shortcut key.
  kColumnGap1,          // Padding region between shortcut and candidate string.
  kColumnCandidate,     // Show candidate string value.
  kColumnDescription,   // Show description annotation message.
  kNumberOfColumns,     // Number of columns. (This item should be last).
};

}  // namespace renderer
}  // namespace mozc

// CandidateView is an NSView subclass to draw the candidate window
// according to the current candidates.
@interface CandidateView : NSView

// setCandidateWindow: sets the candidate window to be rendered.
- (void)setCandidateWindow:(const mozc::commands::CandidateWindow *)candidate_window;

// Sets the host writing direction resolved by CandidateController.
- (void)setWritingDirection:(mozc::renderer::mac::WritingDirection)writing_direction;

// setController: sets the reference of MozcImkInputController.
// It will be used when mouse clicks.  It doesn't take ownerships of
// |controller|.
- (void)setSendCommandInterface:(mozc::client::SendCommandInterface *)command_sender;

// Recalculates the active horizontal or vertical candidate layout and returns
// the size necessary to draw all GUI elements.
- (NSSize)updateLayout;

// Returns the table layout of the current candidates.
// Kept for compatibility with existing TableLayout consumers.
- (const mozc::renderer::TableLayout *)tableLayout;

// Returns the point inside the candidate window that should align to the
// composition target. The concrete layout engine stays private to this view.
- (mozc::Point)candidateAnchorOffset;

// Returns the rectangle used to anchor a cascading candidate window.
// For the current horizontal path this preserves the historical row +
// scrollbar geometry exactly.
- (mozc::Rect)cascadingAnchorRectForRow:(size_t)row;
@end
