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

#include "renderer/qt/qt_window_manager.h"

#include <QPainterPath>
#include <QGraphicsEffect>
#include <QImage>
#include <QScreen>
#include <QPolygon>
#include <QRegion>
#include <QScrollBar>
#include <QStyle>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <limits>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/coordinates.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "renderer/qt/qt_candidate_footer.h"
#include "renderer/renderer_style_handler.h"
#include "renderer/window_util.h"
#include "renderer/window_effect_util.h"

namespace mozc {
namespace renderer {

namespace {
constexpr int kMarginHeight = 5;
constexpr int kMarginWidth = 20;
constexpr int kColumn0Width = 20;
constexpr int kColumn3Width = 6;
// A passive page marker; not QScrollBar (which would desynchronize selection).
constexpr int kPageIndicatorWidth = 6;
constexpr int kInfolistMinWidth = 220;
constexpr int kInfolistMaxWidth = 520;
constexpr int kInfolistScreenMargin = 12;
constexpr int kInfolistTextPadding = 20;

constexpr int kHasInformationRole = Qt::UserRole;
constexpr int kIsFocusedRole = Qt::UserRole + 1;
constexpr int kIsFooterRole = Qt::UserRole + 2;

QString QStr(absl::string_view str) {
  return QString::fromUtf8(str.data(), str.size());
}

QColor QColorFromRGBAColor(RendererStyle::RGBAColor rgba) {
  return QColor(rgba.r(), rgba.g(), rgba.b(), 255 * rgba.a());
}

// The renderer style uses logical text pixels (not points). Explicitly set the
// per-cell font before computing metrics, otherwise window size controls have
// no effect in Qt and the computed table width/height clips larger fonts.
void ApplyTextStyle(QTableWidgetItem* item,
                    const RendererStyle::TextStyle& text_style,
                    const QFont& base_font) {
  QFont font = base_font;
  if (!text_style.font_name().empty()) {
    font.setFamily(QString::fromStdString(text_style.font_name()));
  }
  if (text_style.font_size() > 0) {
    font.setPixelSize(std::max(1, static_cast<int>(
                                      std::lround(text_style.font_size()))));
  }
  font.setWeight(static_cast<QFont::Weight>(
      std::clamp(text_style.font_weight(), 100, 900)));
  item->setFont(font);
}

// The Qt graphics effect handles the smooth visual clip. Removing the old
// binary QRegion mask is essential: a native region would clip even the
// antialiased edge pixels. For popup windows, clicks near the tiny transparent
// corners are still checked by the table's normal hit-testing logic.
void ApplyCandidateWindowShape(QTableWidget* table,
                               QtRoundedClipEffect* effect,
                               uint32_t corner_radius,
                               uint32_t opacity_percent) {
  if (!table || !effect) return;
  const qreal opacity =
      static_cast<qreal>(std::clamp(opacity_percent, 20u, 100u)) / 100.0;
  if (std::abs(table->windowOpacity() - opacity) > 0.001) {
    table->setWindowOpacity(opacity);
  }
  const int radius = std::min({static_cast<int>(std::min(corner_radius, 24u)),
                               table->width() / 2, table->height() / 2});
  effect->SetRadius(radius);
  if (!table->mask().isEmpty()) table->clearMask();
}

}  // namespace

void QtRoundedClipEffect::SetRadius(int radius) {
  radius = std::clamp(radius, 0, 24);
  if (radius_ == radius) return;
  radius_ = radius;
  update();
}

void QtRoundedClipEffect::draw(QPainter* painter) {
  if (radius_ <= 0) {
    drawSource(painter);
    return;
  }
  QPoint offset;
  const QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &offset,
                                     QGraphicsEffect::NoPad);
  if (source.isNull()) return;
  // Render to a premultiplied image at the source's native DPR, preserving
  // subpixel coverage at 125%, 150%, and 200% monitor scaling.
  QImage clipped(source.size(), QImage::Format_ARGB32_Premultiplied);
  clipped.setDevicePixelRatio(source.devicePixelRatio());
  clipped.fill(Qt::transparent);
  QPainter clip(&clipped);
  clip.setRenderHint(QPainter::Antialiasing);
  QPainterPath rounded;
  const qreal dpr = source.devicePixelRatio();
  rounded.addRoundedRect(QRectF(0, 0, source.width() / dpr,
                                source.height() / dpr), radius_, radius_);
  clip.setClipPath(rounded);
  clip.drawPixmap(QPoint(0, 0), source);
  clip.end();
  painter->drawImage(offset, clipped);
}

QtWindowShadow::QtWindowShadow()
    : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint |
                            Qt::WindowStaysOnTopHint |
                            Qt::WindowTransparentForInput) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  hide();
}

void QtWindowShadow::UpdateFor(
    const QWidget* owner, uint32_t corner_radius,
    const RendererStyleHandler::WindowShadowStyle& style) {
  if (!owner || style.size == 0 || style.opacity_percent == 0) {
    hide();
    return;
  }
  const QScreen* screen = owner->screen();
  const qreal dpr = std::clamp(screen ? screen->devicePixelRatio() : 1.0,
                               1.0, 4.0);
  const int physical_width = std::max(1, qRound(owner->width() * dpr));
  const int physical_height = std::max(1, qRound(owner->height() * dpr));
  const int physical_size = qRound(std::min(style.size, 96u) * dpr);
  const int physical_distance = qRound(std::min(style.distance, 96u) * dpr);
  const int physical_radius = qRound(std::min(corner_radius, 24u) * dpr);
  const std::array<int, 8> key = {
      physical_width, physical_height, physical_size, physical_distance,
      physical_radius, static_cast<int>(style.angle_degrees % 360u),
      static_cast<int>(std::min(style.opacity_percent, 100u)), qRound(dpr * 1000)};
  const WindowShadowGeometry shadow_geometry = ComputeWindowShadowGeometry(
      physical_width, physical_height, physical_size, physical_distance,
      style.angle_degrees, physical_radius);
  constexpr int64_t kMaxPixels = 6000000;
  const int64_t pixel_count =
      static_cast<int64_t>(shadow_geometry.width) * shadow_geometry.height;
  if (!shadow_geometry.enabled() || pixel_count <= 0 || pixel_count > kMaxPixels) {
    hide();
    return;
  }
  const bool image_changed = !has_cache_ || cache_key_ != key;
  if (image_changed) {
    std::vector<uint8_t> mask(static_cast<size_t>(pixel_count));
    if (!RenderWindowShadowAlphaMask(
            shadow_geometry, style.opacity_percent, mask.data(), mask.size())) {
      hide();
      return;
    }
    QImage image(shadow_geometry.width, shadow_geometry.height,
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
      hide();
      return;
    }
    image.setDevicePixelRatio(dpr);
    for (int y = 0; y < image.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        row[x] = qRgba(0, 0, 0, mask[static_cast<size_t>(y) * image.width() + x]);
      }
    }
    image_ = std::move(image);
    cache_key_ = key;
    has_cache_ = true;
  }
  const QSize target_size(qCeil(shadow_geometry.width / dpr),
                          qCeil(shadow_geometry.height / dpr));
  const QPoint target_pos = owner->pos() -
      QPoint(qRound(shadow_geometry.owner_left / dpr),
             qRound(shadow_geometry.owner_top / dpr));
  const QRect target_geometry(target_pos, target_size);
  const bool geometry_changed = geometry() != target_geometry;
  if (geometry_changed) setGeometry(target_geometry);
  if (!isVisible()) show();
  if (image_changed || geometry_changed) update();
}

void QtWindowShadow::paintEvent(QPaintEvent*) {
  if (image_.isNull()) return;
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.drawImage(QPoint(0, 0), image_);
}

void CandidateTableDelegate::paint(QPainter* painter,
                                  const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
  const QRect rect = option.rect;
  if (index.data(kIsFooterRole).toBool()) {
    QRect content = rect;
    for (int i = 0; i < separators_.size() && i < content.height(); ++i) {
      painter->fillRect(content.left(), content.top() + i, content.width(), 1,
                        separators_[i]);
    }
    const int separator_height =
        std::min(static_cast<int>(separators_.size()), content.height());
    content.adjust(0, separator_height, 0, 0);
    if (content.height() > 0) {
      QLinearGradient gradient(content.topLeft(), content.bottomLeft());
      gradient.setColorAt(0.0, footer_top_);
      gradient.setColorAt(1.0, footer_bottom_);
      painter->fillRect(content, gradient);
    }
  }
  QStyledItemDelegate::paint(painter, option, index);
  if (index.data(kIsFocusedRole).toBool()) {
    painter->fillRect(rect.left(), rect.top(), rect.width(), 1,
                      focused_border_);
    painter->fillRect(rect.left(), rect.bottom(), rect.width(), 1,
                      focused_border_);
    if (index.column() == 0) {
      painter->fillRect(rect.left(), rect.top(), 1, rect.height(),
                        focused_border_);
    }
    if (index.column() == index.model()->columnCount() - 1) {
      painter->fillRect(rect.right(), rect.top(), 1, rect.height(),
                        focused_border_);
    }
  }
  if (index.data(kHasInformationRole).toBool()) {
    const QRect marker_rect = rect.adjusted(0, 2, -2, -2);
    if (marker_rect.width() > 0 && marker_rect.height() > 0) {
      painter->fillRect(marker_rect, information_marker_);
    }
  }
}

CandidatePageIndicator::CandidatePageIndicator(QWidget* parent)
    : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  hide();
}

void CandidatePageIndicator::Configure(int total, int first, int visible,
                                      QColor track, QColor thumb) {
  total_ = std::max(0, total);
  first_ = std::clamp(first, 0, std::max(0, total_ - 1));
  visible_ = std::clamp(visible, 0, total_);
  track_ = track;
  thumb_ = thumb;
  setVisible(total_ > visible_ && visible_ > 0);
  update();
}

void CandidatePageIndicator::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (total_ <= visible_ || visible_ <= 0 || height() <= 0) return;
  QPainter painter(this);
  painter.fillRect(rect(), track_);
  const int thumb_height = std::clamp(
      static_cast<int>((static_cast<double>(visible_) / total_) * height()),
      3, height());
  const int range = std::max(1, total_ - visible_);
  const int y = (height() - thumb_height) * std::min(first_, range) / range;
  painter.fillRect(0, y, width(), thumb_height, thumb_);
}

QtWindowManager::QtWindowManager() {
  RendererStyleHandler::GetRendererStyle(&style_);
}

void QtWindowManager::OnClicked(int row, int column) {
  DLOG(INFO) << "OnClicked: (" << row << ", " << column << ")";
  if (send_command_interface_ == nullptr) {
    return;
  }
  if (row < 0 ||
      row >= prev_command_.output().candidate_window().candidate_size()) {
    return;
  }
  const int cand_id =
      prev_command_.output().candidate_window().candidate(row).id();
  commands::SessionCommand command;
  command.set_type(commands::SessionCommand::SELECT_CANDIDATE);
  command.set_id(cand_id);
  commands::Output output;
  send_command_interface_->SendCommand(command, &output);
}

void QtWindowManager::Initialize() {
  const auto initialize_table = [](QTableWidget* table) {
    table->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint |
                          Qt::WindowStaysOnTopHint);
    // The rounded clip runs on an ARGB top-level surface.
    table->setAttribute(Qt::WA_TranslucentBackground);
    table->setAttribute(Qt::WA_NoSystemBackground);
    table->setSelectionMode(QTableWidget::NoSelection);
    table->setShowGrid(false);

    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->horizontalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->horizontalHeader()->setMinimumSectionSize(1);

    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->verticalHeader()->hide();
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setMinimumSectionSize(1);
  };

  candidates_ = new QTableWidget();
  initialize_table(candidates_);
  candidate_delegate_ = new CandidateTableDelegate(candidates_);
  candidates_->setItemDelegate(candidate_delegate_);
  page_indicator_ = new CandidatePageIndicator(candidates_);
  candidate_clip_ = new QtRoundedClipEffect(candidates_);
  candidates_->setGraphicsEffect(candidate_clip_);
  candidate_shadow_ = new QtWindowShadow;
  QObject::connect(candidates_, &QTableWidget::cellClicked,
                   [&](int row, int col) { OnClicked(row, col); });

  infolist_ = new QTableWidget();
  initialize_table(infolist_);
  infolist_->setColumnCount(1);
  infolist_->setRowCount(3);
  infolist_->setColumnWidth(0, kInfolistMinWidth);
  infolist_->setWordWrap(true);
  infolist_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  infolist_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  infolist_clip_ = new QtRoundedClipEffect(infolist_);
  infolist_->setGraphicsEffect(infolist_clip_);
  infolist_shadow_ = new QtWindowShadow;
  ApplyStyleToWidgets();
}

void QtWindowManager::ApplyStyleToWidgets() {
  if (candidates_ == nullptr || infolist_ == nullptr) return;
  auto apply = [](QTableWidget* table, const QColor& background,
                  const QColor& foreground) {
    QPalette palette = table->palette();
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::Base, background);
    palette.setColor(QPalette::Text, foreground);
    table->setPalette(palette);
    table->viewport()->setAutoFillBackground(true);
  };
  apply(candidates_,
        QColorFromRGBAColor(style_.candidate_style().background_color()),
        QColorFromRGBAColor(style_.candidate_style().foreground_color()));
  apply(infolist_,
        QColorFromRGBAColor(style_.infolist_style().title_style().background_color()),
        QColorFromRGBAColor(style_.infolist_style().title_style().foreground_color()));

  // A stylesheet (not only palette) is required for Qt to honor a custom
  // frame and to avoid stale system colors after switching dark/light themes.
  const auto apply_frame = [](QTableWidget* table, int thickness,
                              const QColor& border, const QColor& background,
                              const QColor& foreground) {
    table->setStyleSheet(QStringLiteral(
        "QTableWidget { border: %1px solid %2; background-color: %3; "
        "color: %4; }")
        .arg(thickness)
        .arg(border.name())
        .arg(background.name())
        .arg(foreground.name()));
  };
  apply_frame(candidates_, std::clamp(style_.window_border(), 0, 4),
              QColorFromRGBAColor(style_.border_color()),
              QColorFromRGBAColor(style_.candidate_style().background_color()),
              QColorFromRGBAColor(style_.candidate_style().foreground_color()));
  apply_frame(infolist_,
              std::clamp(style_.infolist_style().window_border(), 0, 4),
              QColorFromRGBAColor(style_.infolist_style().border_color()),
              QColorFromRGBAColor(
                  style_.infolist_style().title_style().background_color()),
              QColorFromRGBAColor(
                  style_.infolist_style().title_style().foreground_color()));
  if (candidate_delegate_ != nullptr) {
    QList<QColor> separators;
    for (const auto& color : style_.footer_border_colors()) {
      separators.push_back(QColorFromRGBAColor(color));
    }
    candidate_delegate_->SetColors(
        QColorFromRGBAColor(style_.footer_top_color()),
        QColorFromRGBAColor(style_.footer_bottom_color()),
        std::move(separators),
        QColorFromRGBAColor(style_.focused_border_color()),
        QColorFromRGBAColor(style_.scrollbar_indicator_color()));
  }
}

void QtWindowManager::RefreshCurrentStyle() {
  if (candidates_ == nullptr || !candidates_->isVisible() ||
      !prev_command_.has_output()) {
    return;
  }
  const commands::RendererCommand last = prev_command_;
  prev_command_.Clear();
  UpdateLayout(last);
}

void QtWindowManager::HideAllWindows() {
  candidates_->hide();
  infolist_->hide();
  candidate_shadow_->hide();
  infolist_shadow_->hide();
}

void QtWindowManager::ShowAllWindows() {
  // The shadow windows are intentionally not shown without their owner.
  candidates_->show();
  infolist_->show();
}

// static
bool QtWindowManager::ShouldShowCandidateWindow(
    const commands::RendererCommand& command) {
  if (!command.visible()) {
    return false;
  }

  DCHECK(command.has_output());
  const commands::Output& output = command.output();

  if (!output.has_candidate_window()) {
    return false;
  }

  const commands::CandidateWindow& candidate_window = output.candidate_window();
  if (candidate_window.candidate_size() == 0) {
    return false;
  }

  return true;
}

namespace {
// Copied from unix/candidate_window.cc
void GetDisplayString(const commands::CandidateWindow::Candidate& candidate,
                      std::string& shortcut, std::string& value,
                      std::string& description) {
  shortcut.clear();
  value.clear();
  description.clear();

  if (!candidate.has_value()) {
    return;
  }
  value.assign(candidate.value());

  if (!candidate.has_annotation()) {
    return;
  }

  const commands::Annotation& annotation = candidate.annotation();

  if (annotation.has_shortcut()) {
    shortcut.assign(annotation.shortcut());
  }

  if (annotation.has_description()) {
    description.assign(annotation.description());
  }

  if (annotation.has_prefix()) {
    value.assign(annotation.prefix());
    value.append(candidate.value());
  }

  if (annotation.has_suffix()) {
    value.append(annotation.suffix());
  }
}

Rect GetRect(const QRect& qrect) {
  return Rect(qrect.x(), qrect.y(), qrect.width(), qrect.height());
}

Rect GetRect(const commands::RendererCommand::Rectangle& prect) {
  const int width = prect.right() - prect.left();
  const int height = prect.bottom() - prect.top();
  return Rect(prect.left(), prect.top(), width, height);
}

bool IsUpdated(const commands::RendererCommand& prev_command,
               const commands::RendererCommand& new_command) {
  if (!prev_command.has_output() || !new_command.has_output()) {
    return true;
  }
  const commands::CandidateWindow& prev_cands =
      prev_command.output().candidate_window();
  const commands::CandidateWindow& new_cands =
      new_command.output().candidate_window();
  // Comparing only the first candidate misses updated text/annotations farther
  // down the page, new footer hints, and total-count changes.
  if (prev_cands.candidate_size() != new_cands.candidate_size() ||
      prev_cands.size() != new_cands.size() ||
      prev_cands.category() != new_cands.category() ||
      prev_cands.footer().SerializeAsString() !=
          new_cands.footer().SerializeAsString()) {
    return true;
  }
  for (int i = 0; i < new_cands.candidate_size(); ++i) {
    if (prev_cands.candidate(i).SerializeAsString() !=
        new_cands.candidate(i).SerializeAsString()) {
      return true;
    }
  }
  return false;
}

int GetItemWidth(const QTableWidgetItem& item) {
  QFontMetrics metrics(item.font());
  return metrics.horizontalAdvance(item.text()) + kMarginWidth;
}

int GetItemHeight(const QTableWidgetItem& item) {
  QFontMetrics metrics(item.font());
  return metrics.height() + kMarginHeight;
}

// Width of the longest explicit line, measured with the actual configured
// per-row font. Wrapped text is constrained separately by the display width.
int GetInfolistNaturalWidth(const QTableWidgetItem& item) {
  const QFontMetrics metrics(item.font());
  int max_width = 0;
  for (const QString& line : item.text().split(QLatin1Char('\n'))) {
    max_width = std::max(max_width, metrics.horizontalAdvance(line));
  }
  return max_width + kInfolistTextPadding;
}

// Use Qt text layout metrics instead of estimating line count from the full
// string width. This accounts for explicit newlines and wrapping in Japanese.
int GetInfolistWrappedHeight(const QTableWidgetItem& item, int text_width) {
  const QFontMetrics metrics(item.font());
  const QRect bounds = metrics.boundingRect(
      QRect(0, 0, std::max(1, text_width), 1000000),
      Qt::TextWordWrap | Qt::TextWrapAnywhere, item.text());
  return std::max(metrics.height(), bounds.height()) + 2 * kMarginHeight;
}

int GetFocusedRow(const commands::CandidateWindow& candidate_window) {
  if (candidate_window.has_focused_index()) {
    return candidate_window.focused_index() -
           candidate_window.candidate(0).index();
  }
  return -1;  // No focused row
}

void FillCandidateHighlight(const commands::CandidateWindow& candidate_window,
                            const int row, const RendererStyle& style,
                            QTableWidget* table) {
  if (row < 0 || row >= candidate_window.candidate_size() ||
      row >= table->rowCount()) {
    return;
  }

  // Decoration is handled by CandidateTableDelegate so the border is a
  // continuous frame across all four cells, not four independent boxes.
  table->item(row, 3)->setData(
      kHasInformationRole,
      candidate_window.candidate(row).has_information_id());
  const bool focused = row == GetFocusedRow(candidate_window);
  for (int col = 0; col < table->columnCount(); ++col) {
    table->item(row, col)->setData(kIsFocusedRole, focused);
  }
  if (focused) {
    const QBrush highlight =
        QBrush(QColorFromRGBAColor(style.focused_background_color()));
    for (int col = 0; col < table->columnCount(); ++col) {
      table->item(row, col)->setBackground(highlight);
    }
    return;
  }

  const QBrush background = QBrush(
      QColorFromRGBAColor(style.candidate_style().background_color()));
  if (candidate_window.candidate(row).annotation().shortcut().empty()) {
    table->item(row, 0)->setBackground(background);
  } else {
    table->item(row, 0)->setBackground(
        QBrush(QColorFromRGBAColor(style.shortcut_style().background_color())));
  }
  for (int col = 1; col < table->columnCount(); ++col) {
    table->item(row, col)->setBackground(background);
  }
}

void FillCandidateWindow(const commands::CandidateWindow& candidate_window,
                         const RendererStyle& style, QTableWidget* table) {
  const size_t cands_size = candidate_window.candidate_size();
  table->clearSpans();
  table->clear();
  const QtCandidateFooter footer = GetQtCandidateFooter(candidate_window);
  table->setRowCount(cands_size + (footer.visible ? 1 : 0));
  table->setColumnCount(4);
  table->setColumnWidth(0, kColumn0Width);  // shortcut
  table->setColumnWidth(3, kColumn3Width);  // infolist indicator

  int max_width0 = kColumn0Width;
  int max_width1 = 0;
  int max_width2 = 0;
  int total_height = 0;

  const QBrush shortcut_brush =
      QBrush(QColorFromRGBAColor(style.shortcut_style().foreground_color()));
  const QBrush candidate_brush =
      QBrush(QColorFromRGBAColor(style.candidate_style().foreground_color()));
  const QBrush description_brush =
      QBrush(QColorFromRGBAColor(style.description_style().foreground_color()));
  const QBrush footer_text_brush =
      QBrush(QColorFromRGBAColor(style.footer_style().foreground_color()));

  // Fill the candidates
  std::string shortcut, value, description;
  for (size_t i = 0; i < cands_size; ++i) {
    const commands::CandidateWindow::Candidate& candidate =
        candidate_window.candidate(i);
    GetDisplayString(candidate, shortcut, value, description);

    // shortcut
    auto item0 = new QTableWidgetItem(QStr(shortcut));
    ApplyTextStyle(item0, style.shortcut_style(), table->font());
    item0->setForeground(shortcut_brush);
    item0->setTextAlignment(Qt::AlignCenter);
    table->setItem(i, 0, item0);

    // value
    auto item1 = new QTableWidgetItem(QStr(value));
    ApplyTextStyle(item1, style.candidate_style(), table->font());
    item1->setForeground(candidate_brush);
    table->setItem(i, 1, item1);

    // description
    auto item2 = new QTableWidgetItem(QStr(description));
    ApplyTextStyle(item2, style.description_style(), table->font());
    item2->setForeground(description_brush);
    table->setItem(i, 2, item2);

    // indicator
    auto item3 = new QTableWidgetItem();
    table->setItem(i, 3, item3);
    FillCandidateHighlight(candidate_window, i, style, table);

    max_width0 = std::max(max_width0, GetItemWidth(*item0));
    max_width1 = std::max(max_width1, GetItemWidth(*item1));
    max_width2 = std::max(max_width2, GetItemWidth(*item2));
    const int height = std::max({GetItemHeight(*item0), GetItemHeight(*item1),
                                 GetItemHeight(*item2)});
    table->setRowHeight(i, height);
    total_height += height;
  }

  // A passive suggestion must have no footer whatsoever, not even an
  // empty gradient row. This is the same rule used by Windows and macOS.
  if (footer.visible) {
    for (int i = 0; i < table->columnCount(); ++i) {
      auto* footer_item = new QTableWidgetItem();
      ApplyTextStyle(footer_item, style.footer_style(), table->font());
      footer_item->setForeground(footer_text_brush);
      footer_item->setData(kIsFooterRole, true);
      table->setItem(cands_size, i, footer_item);
    }
    auto* footer0 = table->item(cands_size, 0);
    footer0->setText(QStr(footer.label));
    if (footer.is_sub_label) {
      ApplyTextStyle(footer0, style.footer_sub_label_style(), table->font());
    }
    footer0->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setSpan(cands_size, 0, 1, 2);
    if (!footer.label.empty()) {
      const int left_width = std::min(360, GetItemWidth(*footer0) + kMarginWidth);
      max_width1 += std::max(0, left_width - (max_width0 + max_width1));
    }
    auto* footer2 = table->item(cands_size, 2);
    footer2->setText(QStr(footer.index));
    footer2->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Merge the unused information-marker column only in the footer.
    // Without this, a blank six-pixel strip sits to the right of "1/101".
    table->setSpan(cands_size, 2, 1, 2);
    const QFontMetrics footer_metrics(footer2->font());
    constexpr int kFooterIndexRightPadding = 5;
    max_width2 = std::max(
        max_width2,
        std::max(0, footer_metrics.horizontalAdvance(footer2->text()) +
                        kFooterIndexRightPadding - kColumn3Width));
    const int footer_height =
        std::max(GetItemHeight(*footer0), GetItemHeight(*footer2)) +
        style.footer_border_colors_size();
    table->setRowHeight(cands_size, footer_height);
    total_height += footer_height;
  }

  // Resize
  table->setColumnWidth(0, max_width0);
  table->setColumnWidth(1, max_width1);
  table->setColumnWidth(2, max_width2);
  const int border = std::clamp(style.window_border(), 0, 4);
  const int width = max_width0 + max_width1 + max_width2 + kColumn3Width +
                    (candidate_window.size() > candidate_window.candidate_size()
                         ? kPageIndicatorWidth : 0);
  table->resize(width + 2 * border, total_height + 2 * border);
}

class VirtualRect {
 public:
  static VirtualRect FromNativeRect(const Rect& native_rect) {
    for (const QScreen* screen : QGuiApplication::screens()) {
      const Rect rect = TranslateToVirtual(screen, native_rect);
      const QRect screen_rect = screen->geometry();

      // Use top left to locate a screen
      if (screen_rect.contains(rect.Left(), rect.Top())) {
        return VirtualRect(rect, mozc::renderer::GetRect(screen->availableGeometry()));
      }
    }

    // If the caret is just outside a monitor (or mixed-DPI conversion is
    // ambiguous), pick the closest available working area, not the primary
    // monitor unconditionally.
    const QScreen* screen = QGuiApplication::primaryScreen();
    qint64 best_distance = std::numeric_limits<qint64>::max();
    for (const QScreen* candidate : QGuiApplication::screens()) {
      const Rect rect = TranslateToVirtual(candidate, native_rect);
      const QRect area = candidate->availableGeometry();
      const qint64 dx = std::max<qint64>(
          {static_cast<qint64>(area.left()) - rect.Left(), 0,
           static_cast<qint64>(rect.Left()) - area.right()});
      const qint64 dy = std::max<qint64>(
          {static_cast<qint64>(area.top()) - rect.Top(), 0,
           static_cast<qint64>(rect.Top()) - area.bottom()});
      const qint64 distance = dx * dx + dy * dy;
      if (distance < best_distance) {
        best_distance = distance;
        screen = candidate;
      }
    }
    if (screen == nullptr) return VirtualRect(native_rect, native_rect);
    const Rect rect = TranslateToVirtual(screen, native_rect);
    return VirtualRect(rect, mozc::renderer::GetRect(screen->availableGeometry()));
  }

  Rect GetRect() const { return rect_; }

  Rect GetMonitorRect() const { return monitor_rect_; }

 private:
  VirtualRect(const Rect& rect, const Rect& monitor_rect)
      : rect_(rect), monitor_rect_(monitor_rect) {}

  static Rect TranslateToVirtual(const QScreen* screen,
                                 const Rect& native_rect) {
    const double device_pixel_ratio = screen->devicePixelRatio();
    // screen_left, screen_top have the same value in both virtual and native
    // coordinate
    const int screen_left = screen->geometry().x();
    const int screen_top = screen->geometry().y();
    const int dx = native_rect.Left() - screen_left;
    const int dy = native_rect.Top() - screen_top;
    const int vx =
        static_cast<int>(std::floor(dx / device_pixel_ratio)) + screen_left;
    const int vy =
        static_cast<int>(std::floor(dy / device_pixel_ratio)) + screen_top;

    return Rect(vx, vy, native_rect.Width() / device_pixel_ratio,
                native_rect.Height() / device_pixel_ratio);
  }

  Rect rect_;
  Rect monitor_rect_;
};
}  // namespace

Point QtWindowManager::GetWindowPosition(
    const commands::RendererCommand& command, const Size& win_size) {
  const Rect native_preedit_rect = GetRect(command.preedit_rectangle());
  // Qt6 applications use virtual coordinates. Since IBus uses the device-pixel
  // native coordinate system, we need to translate a received rect to virtual.
  const VirtualRect virtual_rect =
      VirtualRect::FromNativeRect(native_preedit_rect);
  const Rect preedit_rect = virtual_rect.GetRect();
  const Point win_pos = Point(preedit_rect.Left(), preedit_rect.Bottom());
  const Rect monitor_rect = virtual_rect.GetMonitorRect();
  const Point offset_to_column1(kColumn0Width, 0);

  const Rect adjusted_win_geometry =
      WindowUtil::GetWindowRectForMainWindowFromTargetPointAndPreedit(
          win_pos, preedit_rect, win_size, offset_to_column1, monitor_rect,
          /* vertical */ false);  // Only support horizontal window yet.
  return adjusted_win_geometry.origin;
}

Rect QtWindowManager::UpdateCandidateWindow(
    const commands::RendererCommand& command) {
  const commands::CandidateWindow& candidate_window =
      command.output().candidate_window();

  RendererStyle selected_style;
  const auto kind =
      RendererStyleHandler::GetRendererStyleTypeForCandidateWindow(candidate_window);
  if (RendererStyleHandler::GetRendererStyleForWindowType(kind,
                                                         &selected_style) &&
      (kind != active_style_type_ ||
       selected_style.SerializeAsString() != style_.SerializeAsString())) {
    style_ = std::move(selected_style);
    active_style_type_ = kind;
    ApplyStyleToWidgets();
    // A style change needs a full refill, not only a focused-row update.
    prev_command_.Clear();
  }

  if (IsUpdated(prev_command_, command)) {
    FillCandidateWindow(candidate_window, style_, candidates_);
  } else {
    // Reset the previous focused highlight
    const int prev_focused =
        GetFocusedRow(prev_command_.output().candidate_window());
    FillCandidateHighlight(candidate_window, prev_focused, style_, candidates_);
  }

  // Window shape and position must be recomputed even for focus-only updates.
  // The same candidate list can move when the text caret or monitor changes.
  const auto effects =
      RendererStyleHandler::GetCandidateWindowEffectStyle(active_style_type_);
  const uint32_t corner_radius =
      RendererStyleHandler::GetCandidateWindowCornerRadius(active_style_type_);
  ApplyCandidateWindowShape(candidates_, candidate_clip_, corner_radius,
                            effects.opacity_percent);
  const Size win_size(candidates_->width(), candidates_->height());
  const Point win_pos = GetWindowPosition(command, win_size);
  candidates_->move(win_pos.x, win_pos.y);

  // Set the focused highlight
  FillCandidateHighlight(candidate_window, GetFocusedRow(candidate_window),
                         style_, candidates_);

  // Footer index. Keep the page indicator outside the selectable rows; a
  // native Qt scrollbar would scroll the table independently of Mozc's pages.
  const QtCandidateFooter footer = GetQtCandidateFooter(candidate_window);
  if (footer.visible) {
    candidates_->item(candidates_->rowCount() - 1, 2)
        ->setText(QStr(footer.index));
  }
  const int border = std::clamp(style_.window_border(), 0, 4);
  const int footer_height =
      footer.visible ? candidates_->rowHeight(candidates_->rowCount() - 1) : 0;
  page_indicator_->setGeometry(
      candidates_->width() - border - kPageIndicatorWidth, border,
      kPageIndicatorWidth,
      std::max(0, candidates_->height() - 2 * border - footer_height));
  page_indicator_->Configure(
      candidate_window.size(), candidate_window.candidate(0).index(),
      candidate_window.candidate_size(),
      QColorFromRGBAColor(style_.scrollbar_background_color()),
      QColorFromRGBAColor(style_.scrollbar_indicator_color()));
  page_indicator_->raise();

  candidate_shadow_->UpdateFor(candidates_, corner_radius, effects.shadow);
  candidates_->show();
  candidates_->raise();
  prev_command_ = command;
  return GetRect(candidates_->geometry());
}

bool QtWindowManager::ShouldShowInfolistWindow(
    const commands::RendererCommand& command) {
  if (!command.output().has_candidate_window()) {
    return false;
  }

  const commands::CandidateWindow& candidate_window =
      command.output().candidate_window();
  if (candidate_window.candidate_size() <= 0) {
    return false;
  }

  if (!candidate_window.has_usages() || !candidate_window.has_focused_index()) {
    return false;
  }

  if (candidate_window.usages().information_size() <= 0) {
    return false;
  }

  // Converts candidate's index to column row index.
  const int focused_row =
      candidate_window.focused_index() - candidate_window.candidate(0).index();
  if (focused_row < 0 || focused_row >= candidate_window.candidate_size()) {
    return false;
  }

  if (!candidate_window.candidate(focused_row).has_information_id()) {
    return false;
  }

  return true;
}

Rect QtWindowManager::GetMonitorRect(int x, int y) {
  QPoint point{x, y};
  const QScreen* screen = QGuiApplication::screenAt(point);
  if (screen == nullptr) {
    // (x, y) does not belong to any screen. Fall back to the primary screen.
    // TODO: Return the nearest monitor rect instead.
    // c.f. GetWorkingAreaFromPointImpl in win32_renderer_util.cc.
    return GetRect(QGuiApplication::primaryScreen()->availableGeometry());
  }

  return GetRect(screen->availableGeometry());
}

void QtWindowManager::UpdateInfolistWindow(
    const commands::RendererCommand& command,
    const Rect& candidate_window_rect) {
  if (!ShouldShowInfolistWindow(command)) {
    infolist_->hide();
    infolist_shadow_->hide();
    return;
  }

  infolist_->clear();

  const commands::InformationList& info =
      command.output().candidate_window().usages();
  const size_t size = info.information_size();

  infolist_->setColumnCount(1);
  infolist_->setRowCount(size * 2 + 1);  // +1 is for the caption title.

  const Rect monitor_rect = GetMonitorRect(candidate_window_rect.Right(),
                                           candidate_window_rect.Top());
  const int border = std::clamp(style_.infolist_style().window_border(), 0, 4);
  int natural_width = kInfolistMinWidth;

  // Caption title
  absl::string_view caption = style_.infolist_style().caption_string();
  QTableWidgetItem* infolist_title = new QTableWidgetItem(QStr(caption));
  ApplyTextStyle(infolist_title, style_.infolist_style().caption_style(),
                 infolist_->font());
  infolist_title->setBackground(QBrush(
      QColorFromRGBAColor(style_.infolist_style().caption_background_color())));
  infolist_title->setForeground(QBrush(
      QColorFromRGBAColor(style_.infolist_style().caption_style().foreground_color())));
  infolist_->setItem(0, 0, infolist_title);
  natural_width = std::max(natural_width, GetInfolistNaturalWidth(*infolist_title));

  for (int i = 0; i < size; ++i) {
    const int title_row = i * 2 + 1;
    const int desc_row = i * 2 + 2;
    const std::string title = info.information(i).title();
    const std::string desc = info.information(i).description();
    auto* qtitle = new QTableWidgetItem(QString::fromUtf8(title.c_str()));
    auto* qdesc = new QTableWidgetItem(QString::fromUtf8(desc.c_str()));
    ApplyTextStyle(qtitle, style_.infolist_style().title_style(),
                   infolist_->font());
    ApplyTextStyle(qdesc, style_.infolist_style().description_style(),
                   infolist_->font());
    qtitle->setForeground(QBrush(QColorFromRGBAColor(
        style_.infolist_style().title_style().foreground_color())));
    qdesc->setForeground(QBrush(QColorFromRGBAColor(
        style_.infolist_style().description_style().foreground_color())));
    // The infolist has a translucent top-level surface for rounded corners.
    // Paint every row explicitly; otherwise unfocused rows remain transparent.
    // The focused row below overrides these base colors as intended.
    qtitle->setBackground(QBrush(QColorFromRGBAColor(
        style_.infolist_style().title_style().background_color())));
    qdesc->setBackground(QBrush(QColorFromRGBAColor(
        style_.infolist_style().description_style().background_color())));

    // setItem(row, column, item): preserve the existing title/description order.
    infolist_->setItem(title_row, 0, qtitle);
    infolist_->setItem(desc_row, 0, qdesc);
    natural_width = std::max({natural_width,
                              GetInfolistNaturalWidth(*qtitle),
                              GetInfolistNaturalWidth(*qdesc)});

    if (info.focused_index() == i) {
      const QBrush highlight = QBrush(
          QColorFromRGBAColor(style_.infolist_style().focused_background_color()));
      qtitle->setBackground(highlight);
      qdesc->setBackground(highlight);
    }
  }

  // A narrow monitor may be smaller than the nominal minimum width.
  const int max_screen_width =
      std::max(1, monitor_rect.Width() - 2 * kInfolistScreenMargin);
  const int width = std::min({std::max(kInfolistMinWidth, natural_width + 2 * border),
                              kInfolistMaxWidth, max_screen_width});
  const int max_screen_height =
      std::max(1, monitor_rect.Height() - 2 * kInfolistScreenMargin);

  // A vertical scrollbar reduces the width available for text. Measure once
  // without it, then remeasure with the scrollbar if clipping is necessary.
  const auto measure_rows = [&](int scrollbar_width) {
    const int text_width = std::max(
        1, width - 2 * border - scrollbar_width - kInfolistTextPadding);
    int height = 12;  // Preserve the existing outer vertical margin.
    for (int row = 0; row < infolist_->rowCount(); ++row) {
      const QTableWidgetItem* item = infolist_->item(row, 0);
      const int row_height = GetInfolistWrappedHeight(*item, text_width);
      infolist_->setRowHeight(row, row_height);
      height += row_height;
    }
    return height + 2 * border;
  };

  int scrollbar_width = 0;
  int content_height = measure_rows(0);
  if (content_height > max_screen_height) {
    scrollbar_width = infolist_->style()->pixelMetric(
        QStyle::PM_ScrollBarExtent, nullptr, infolist_);
    content_height = measure_rows(scrollbar_width);
  }
  const int height = std::min(content_height, max_screen_height);
  infolist_->setColumnWidth(
      0, std::max(1, width - 2 * border - scrollbar_width));
  const Size infolist_size(width, height);
  const Rect infolist_rect = WindowUtil::GetWindowRectForInfolistWindow(
      infolist_size, candidate_window_rect, monitor_rect);

  infolist_->move(infolist_rect.Left(), infolist_rect.Top());
  infolist_->resize(width, height);
  // A new focused entry starts at the top; overflowing descriptions remain
  // accessible through the standard table scrollbar.
  infolist_->verticalScrollBar()->setValue(0);
  // The infolist currently inherits the candidate window effect; its palette
  // remains independent, as defined in RendererStyle::InfolistStyle.
  const auto effects =
      RendererStyleHandler::GetCandidateWindowEffectStyle(active_style_type_);
  const uint32_t radius =
      RendererStyleHandler::GetCandidateWindowCornerRadius(active_style_type_);
  ApplyCandidateWindowShape(infolist_, infolist_clip_, radius,
                            effects.opacity_percent);
  infolist_shadow_->UpdateFor(infolist_, radius, effects.shadow);
  infolist_->show();
  infolist_->raise();
}

void QtWindowManager::UpdateLayout(const commands::RendererCommand& command) {
  if (!ShouldShowCandidateWindow(command)) {
    HideAllWindows();
    return;
  }

  const Rect candidate_window_rect = UpdateCandidateWindow(command);
  UpdateInfolistWindow(command, candidate_window_rect);
}

bool QtWindowManager::Activate() {
  DLOG(INFO) << "Activate";
  return true;
}

bool QtWindowManager::IsAvailable() const {
  DLOG(INFO) << "IsAvailable";
  return true;
}

bool QtWindowManager::ExecCommand(const commands::RendererCommand& command) {
  switch (command.type()) {
    case commands::RendererCommand::NOOP:
      break;
    case commands::RendererCommand::SHUTDOWN:
      // TODO(nona): Implement shutdown command.
      DLOG(ERROR) << "Shutdown command is not implemented.";
      return false;
      break;
    case commands::RendererCommand::UPDATE:
      if (!command.visible()) {
        HideAllWindows();
      } else {
        UpdateLayout(command);
      }
      return true;
      break;
    default:
      LOG(WARNING) << "Unknown command: " << command.type();
      break;
  }
  return true;
}

bool QtWindowManager::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  send_command_interface_ = send_command_interface;
  return true;
}

void QtWindowManager::SetWindowPos(int x, int y) { candidates_->move(x, y); }

}  // namespace renderer
}  // namespace mozc
