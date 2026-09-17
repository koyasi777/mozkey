#include "renderer/win32/vertical_candidate_layout.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

int NonNegative(int value) { return std::max(0, value); }

Size NormalizeSize(const Size& size) {
  if (size.width <= 0 || size.height <= 0) {
    return Size();
  }
  return Size(size.width, size.height);
}

bool HasArea(const Size& size) {
  return size.width > 0 && size.height > 0;
}

int MaxWidth(std::initializer_list<Size> sizes) {
  int width = 0;
  for (const Size& size : sizes) {
    if (HasArea(size)) {
      width = std::max(width, size.width);
    }
  }
  return width;
}

int CandidateWidth(
    const VerticalCandidateLayout::CandidateMetrics& item,
    const VerticalCandidateLayout::Parameters& parameters) {
  const int content_width =
      MaxWidth({item.shortcut_size, item.value_size, item.description_size});
  return content_width + parameters.card_horizontal_padding * 2;
}

int CandidateHeight(
    const VerticalCandidateLayout::CandidateMetrics& item,
    const VerticalCandidateLayout::Parameters& parameters) {
  const int trailing_padding =
      item.has_information_marker
          ? std::max(parameters.card_vertical_padding,
                     parameters.information_marker_tail_reserve)
          : parameters.card_vertical_padding;
  int height = parameters.card_vertical_padding + trailing_padding;

  const bool has_shortcut = HasArea(item.shortcut_size);
  const bool has_value = HasArea(item.value_size);
  const bool has_description = HasArea(item.description_size);
  const bool has_body = has_value || has_description;

  if (has_shortcut) {
    height += item.shortcut_size.height;
  }
  if (has_shortcut && has_body) {
    height += parameters.shortcut_body_gap;
  }
  if (has_value) {
    height += item.value_size.height;
  }
  if (has_value && has_description) {
    height += parameters.value_description_gap;
  }
  if (has_description) {
    height += item.description_size.height;
  }

  return height;
}

int CenteredLeft(const Rect& rect, int width) {
  return rect.Left() + (rect.Width() - width) / 2;
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
  parameters.cross_axis_edge_padding =
      NonNegative(parameters.cross_axis_edge_padding);
  parameters.candidate_gap = NonNegative(parameters.candidate_gap);
  parameters.card_horizontal_padding =
      NonNegative(parameters.card_horizontal_padding);
  parameters.card_vertical_padding =
      NonNegative(parameters.card_vertical_padding);
  parameters.shortcut_body_gap =
      NonNegative(parameters.shortcut_body_gap);
  parameters.value_description_gap =
      NonNegative(parameters.value_description_gap);
  parameters.information_marker_tail_reserve =
      NonNegative(parameters.information_marker_tail_reserve);
  parameters.footer_size = NormalizeSize(parameters.footer_size);

  std::vector<CandidateMetrics> metrics;
  metrics.reserve(input_candidates.size());

  int body_width = 0;
  int body_height = 0;

  for (const CandidateMetrics& input : input_candidates) {
    CandidateMetrics item = input;
    item.shortcut_size = NormalizeSize(item.shortcut_size);
    item.value_size = NormalizeSize(item.value_size);
    item.description_size = NormalizeSize(item.description_size);
    metrics.push_back(item);

    body_width += CandidateWidth(item, parameters);
    body_height =
        std::max(body_height, CandidateHeight(item, parameters));
  }

  if (metrics.size() > 1) {
    body_width +=
        static_cast<int>(metrics.size() - 1) * parameters.candidate_gap;
  }

  const int body_with_edges =
      metrics.empty()
          ? 0
          : body_width + parameters.cross_axis_edge_padding * 2;
  const int inner_width =
      std::max(body_with_edges, parameters.footer_size.width);

  total_size_ =
      Size(parameters.window_border * 2 + inner_width,
           parameters.window_border * 2 + body_height +
               parameters.footer_size.height);

  if (metrics.empty()) {
    footer_rect_ =
        Rect(parameters.window_border, parameters.window_border,
             inner_width, parameters.footer_size.height);
    return;
  }

  // Keep candidate 0 attached to the right flow edge even when a horizontal
  // footer is wider than the vertical candidate body.  Any footer-only surplus
  // width remains on the physical left instead of moving the active candidate
  // away from the composition text.
  int candidate_right =
      total_size_.width - parameters.window_border -
      parameters.cross_axis_edge_padding;
  const int candidate_top = parameters.window_border;

  candidates_.reserve(metrics.size());

  for (const CandidateMetrics& item : metrics) {
    const int candidate_width = CandidateWidth(item, parameters);
    const int candidate_height = CandidateHeight(item, parameters);
    const int candidate_left = candidate_right - candidate_width;

    const Rect candidate_rect(candidate_left, candidate_top,
                              candidate_width, candidate_height);

    int cursor_y =
        candidate_rect.Top() + parameters.card_vertical_padding;

    Rect shortcut_rect;
    if (HasArea(item.shortcut_size)) {
      shortcut_rect =
          Rect(CenteredLeft(candidate_rect, item.shortcut_size.width),
               cursor_y, item.shortcut_size.width,
               item.shortcut_size.height);
      cursor_y += item.shortcut_size.height;
    }

    const bool has_value = HasArea(item.value_size);
    const bool has_description = HasArea(item.description_size);
    const bool has_body = has_value || has_description;

    if (HasArea(item.shortcut_size) && has_body) {
      cursor_y += parameters.shortcut_body_gap;
    }

    Rect value_rect;
    if (has_value) {
      value_rect =
          Rect(CenteredLeft(candidate_rect, item.value_size.width),
               cursor_y, item.value_size.width, item.value_size.height);
      cursor_y += item.value_size.height;
    }

    Rect description_rect;
    if (has_description) {
      if (has_value) {
        cursor_y += parameters.value_description_gap;
      }
      description_rect =
          Rect(CenteredLeft(candidate_rect, item.description_size.width),
               cursor_y, item.description_size.width,
               item.description_size.height);
    }

    candidates_.push_back(CandidateGeometry{
        candidate_rect,
        shortcut_rect,
        value_rect,
        description_rect,
    });

    candidate_right =
        candidate_left - parameters.candidate_gap;
  }

  footer_rect_ =
      Rect(parameters.window_border,
           parameters.window_border + body_height,
           inner_width, parameters.footer_size.height);
}

Rect VerticalCandidateLayout::GetCandidateRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].candidate_rect;
}

Rect VerticalCandidateLayout::GetCandidateSelectionRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }

  const Rect& candidate_rect = candidates_[index].candidate_rect;
  const int selection_height =
      std::max(0, footer_rect_.Top() - candidate_rect.Top());
  return Rect(candidate_rect.Left(), candidate_rect.Top(),
              candidate_rect.Width(), selection_height);
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
