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

#ifndef MOZC_RENDERER_QT_QT_WINDOW_MANAGER_H_
#define MOZC_RENDERER_QT_QT_WINDOW_MANAGER_H_

#include <QtWidgets>

#include <array>

#include <utility>

#include "base/coordinates.h"
#include "client/client_interface.h"
#include "protocol/renderer_command.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/renderer_style_handler.h"

namespace mozc {
namespace renderer {

// Candidate rows share one visual focus frame, and the footer is a gradient.
// The source of truth for colors remains RendererStyle, not a Qt theme preset.
class CandidateTableDelegate : public QStyledItemDelegate {
 public:
  explicit CandidateTableDelegate(QObject* parent)
      : QStyledItemDelegate(parent) {}
  void SetColors(QColor top, QColor bottom, QList<QColor> separators,
                 QColor focused_border, QColor information_marker) {
    footer_top_ = top;
    footer_bottom_ = bottom;
    separators_ = std::move(separators);
    focused_border_ = focused_border;
    information_marker_ = information_marker;
  }
  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;

 private:
  QColor footer_top_;
  QColor footer_bottom_;
  QList<QColor> separators_;
  QColor focused_border_;
  QColor information_marker_;
};

// Passive visual indication of the currently displayed subset of candidates.
// Mouse and keyboard navigation continue to be owned by the IME engine.
class CandidatePageIndicator : public QWidget {
 public:
  explicit CandidatePageIndicator(QWidget* parent);
  void Configure(int total, int first, int visible, QColor track,
                 QColor thumb);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  int total_ = 0;
  int first_ = 0;
  int visible_ = 0;
  QColor track_;
  QColor thumb_;
};

// Compositor-backed, input-transparent shadow. Caches its ARGB image until
// size, DPI, or shadow settings change; a native Qt scrollbar is never used.
class QtWindowShadow : public QWidget {
 public:
  QtWindowShadow();
  void UpdateFor(const QWidget* owner, uint32_t corner_radius,
                 const RendererStyleHandler::WindowShadowStyle& style);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QImage image_;
  std::array<int, 8> cache_key_{};
  bool has_cache_ = false;
};

// Paint the complete table via Qt's graphics-effect pipeline, then use a
// high-DPI antialiased clip mask. This avoids QRegion's staircase-shaped edge.
class QtRoundedClipEffect : public QGraphicsEffect {
 public:
  explicit QtRoundedClipEffect(QObject* parent = nullptr)
      : QGraphicsEffect(parent) {}
  void SetRadius(int radius);

 protected:
  void draw(QPainter* painter) override;

 private:
  int radius_ = 0;
};

class QtWindowManager {
 public:
  QtWindowManager();
  QtWindowManager(const QtWindowManager &) = delete;
  QtWindowManager &operator=(const QtWindowManager &) = delete;
  ~QtWindowManager() = default;

  void Initialize();
  // Repaint an already visible window after the platform theme changes.
  void RefreshCurrentStyle();

  void HideAllWindows();
  void ShowAllWindows();
  void UpdateLayout(const commands::RendererCommand &command);
  bool Activate();
  bool IsAvailable() const;
  bool ExecCommand(const commands::RendererCommand &command);
  bool SetSendCommandInterface(
      client::SendCommandInterface *send_command_interface);
  void SetWindowPos(int x, int y);

 protected:
  // If this function returns true, we should show/reload candidate window.
  bool ShouldShowCandidateWindow(const commands::RendererCommand &command);

  // Judges whether infolist should be shown or not.
  bool ShouldShowInfolistWindow(const commands::RendererCommand &command);

  // Updates candidate window size and location based on given command, and
  // returns actual rectangle.
  Rect UpdateCandidateWindow(const commands::RendererCommand &command);

  // Updates infolist window size and location based on given command and
  // candidate window rectangle.
  void UpdateInfolistWindow(const commands::RendererCommand &command,
                            const Rect &candidate_window_rect);

  // Returns monitor rectangle for the specified point.
  Rect GetMonitorRect(int x, int y);

  Point GetWindowPosition(const commands::RendererCommand &command,
                          const Size &win_size);

  void OnClicked(int row, int column);

 private:
  void ApplyStyleToWidgets();

  QTableWidget *candidates_ = nullptr;
  QTableWidget *infolist_ = nullptr;
  CandidateTableDelegate *candidate_delegate_ = nullptr;
  CandidatePageIndicator *page_indicator_ = nullptr;
  QtWindowShadow *candidate_shadow_ = nullptr;
  QtWindowShadow *infolist_shadow_ = nullptr;
  QtRoundedClipEffect *candidate_clip_ = nullptr;
  QtRoundedClipEffect *infolist_clip_ = nullptr;

  RendererStyle style_;
  RendererStyleHandler::RendererStyleType active_style_type_ =
      RendererStyleHandler::RendererStyleType::kCandidate;
  commands::RendererCommand prev_command_;
  client::SendCommandInterface *send_command_interface_ = nullptr;
};

}  // namespace renderer
}  // namespace mozc
#endif  // MOZC_RENDERER_QT_QT_WINDOW_MANAGER_H_
