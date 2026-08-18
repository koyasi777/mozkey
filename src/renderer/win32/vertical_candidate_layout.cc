#include "renderer/win32/vertical_candidate_layout.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

int NonNegative(int value) {
  return std::max(0, value);
}

Size NonNegativeSize(const Size& size) {
  return Size(NonNegative(size.width), NonNegative(size.height));
}

int MaxWidth(const VerticalCandidateLayout::CandidateMetrics& metrics) {
  return std::max(
      metrics.shortcut_size.width,
      std::max(metrics.value_size.width, metrics.description_size.width));
}

int CenteredLeft(const Rect& column, int content_width) {
  return column.Left() + (column.Width() - content_width) / 2;
}

}  // namespace

void VerticalCandidateLayout::Initialize(
    const std::vector<CandidateMetrics>& input_candidates,
    const Parameters& input_parameters) {
  candidates_.clear();
  total_size_ = Size();
  footer_rect_ = Rect();

  Parameters parameters = input_parameters;
  parameters.window_border = NonNegative(parameters.window_border);
  parameters.column_padding = NonNegative(parameters.column_padding);
  parameters.vertical_padding = NonNegative(parameters.vertical_padding);
  parameters.section_gap = NonNegative(parameters.section_gap);
  parameters.footer_size = NonNegativeSize(parameters.footer_size);

  std::vector<CandidateMetrics> metrics;
  metrics.reserve(input_candidates.size());

  int shortcut_zone_height = 0;
  int value_zone_height = 0;
  int description_zone_height = 0;
  int body_width = 0;

  for (const CandidateMetrics& input : input_candidates) {
    CandidateMetrics item = input;
    item.shortcut_size = NonNegativeSize(item.shortcut_size);
    item.value_size = NonNegativeSize(item.value_size);
    item.description_size = NonNegativeSize(item.description_size);
    metrics.push_back(item);

    shortcut_zone_height =
        std::max(shortcut_zone_height, item.shortcut_size.height);
    value_zone_height = std::max(value_zone_height, item.value_size.height);
    description_zone_height =
        std::max(description_zone_height, item.description_size.height);

    body_width +=
        MaxWidth(item) + parameters.column_padding * 2;
  }

  const bool has_candidates = !metrics.empty();
  const bool has_shortcut_zone = shortcut_zone_height > 0;
  const bool has_value_zone = value_zone_height > 0;
  const bool has_description_zone = description_zone_height > 0;

  int body_height = 0;
  if (has_candidates) {
    body_height = parameters.vertical_padding * 2 + shortcut_zone_height +
                  value_zone_height + description_zone_height;

    int visible_sections = 0;
    visible_sections += has_shortcut_zone ? 1 : 0;
    visible_sections += has_value_zone ? 1 : 0;
    visible_sections += has_description_zone ? 1 : 0;
    if (visible_sections > 1) {
      body_height += parameters.section_gap * (visible_sections - 1);
    }
  }

  const int inner_width = std::max(body_width, parameters.footer_size.width);
  total_size_ =
      Size(parameters.window_border * 2 + inner_width,
           parameters.window_border * 2 + body_height +
               parameters.footer_size.height);

  const int body_top = parameters.window_border;
  int column_right = total_size_.width - parameters.window_border;

  int shortcut_top = body_top + parameters.vertical_padding;
  int value_top = shortcut_top + shortcut_zone_height;
  if (has_shortcut_zone && has_value_zone) {
    value_top += parameters.section_gap;
  }

  int description_top = value_top + value_zone_height;
  if (has_description_zone && (has_shortcut_zone || has_value_zone)) {
    description_top += parameters.section_gap;
  }

  candidates_.reserve(metrics.size());
  for (const CandidateMetrics& item : metrics) {
    const int column_width =
        MaxWidth(item) + parameters.column_padding * 2;
    const int column_left = column_right - column_width;
    const Rect candidate_rect(column_left, body_top, column_width, body_height);

    const Rect shortcut_rect(
        CenteredLeft(candidate_rect, item.shortcut_size.width), shortcut_top,
        item.shortcut_size.width, item.shortcut_size.height);
    const Rect value_rect(
        CenteredLeft(candidate_rect, item.value_size.width), value_top,
        item.value_size.width, item.value_size.height);
    const Rect description_rect(
        CenteredLeft(candidate_rect, item.description_size.width),
        description_top, item.description_size.width,
        item.description_size.height);

    candidates_.push_back(CandidateGeometry{
        candidate_rect,
        shortcut_rect,
        value_rect,
        description_rect,
    });

    // Candidate 0 is rightmost.  Preserve semantic candidate order while
    // advancing visual columns from right to left.
    column_right = column_left;
  }

  footer_rect_ =
      Rect(parameters.window_border, parameters.window_border + body_height,
           inner_width, parameters.footer_size.height);
}

Rect VerticalCandidateLayout::GetCandidateRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].candidate_rect;
}

Rect VerticalCandidateLayout::GetShortcutRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].shortcut_rect;
}

Rect VerticalCandidateLayout::GetValueRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].value_rect;
}

Rect VerticalCandidateLayout::GetDescriptionRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].description_rect;
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
