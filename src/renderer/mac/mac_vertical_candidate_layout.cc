// Copyright 2026, Mozkey authors.
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
//     * Neither the name of Mozkey authors nor the names of its
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

#include "renderer/mac/mac_vertical_candidate_layout.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace mac {
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
    const MacVerticalCandidateLayout::CandidateMetrics& item,
    const MacVerticalCandidateLayout::Parameters& parameters) {
  const int content_width =
      MaxWidth({item.shortcut_size, item.value_size,
                item.description_size, item.information_marker_size});
  return content_width + parameters.card_horizontal_padding * 2;
}

int CandidateHeight(
    const MacVerticalCandidateLayout::CandidateMetrics& item,
    const MacVerticalCandidateLayout::Parameters& parameters) {
  int height = parameters.card_vertical_padding * 2;

  const bool has_shortcut = HasArea(item.shortcut_size);
  const bool has_value = HasArea(item.value_size);
  const bool has_description = HasArea(item.description_size);
  const bool has_information = HasArea(item.information_marker_size);
  const bool has_text = has_value || has_description;

  if (has_shortcut) {
    height += item.shortcut_size.height;
  }
  if (has_shortcut && has_text) {
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
  if (has_information) {
    if (has_shortcut || has_text) {
      height += parameters.information_marker_gap;
    }
    height += item.information_marker_size.height;
  }

  return height;
}

int CenteredLeft(const Rect& rect, int width) {
  return rect.Left() + (rect.Width() - width) / 2;
}

}  // namespace

void MacVerticalCandidateLayout::Initialize(
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
  parameters.information_marker_gap =
      NonNegative(parameters.information_marker_gap);
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
    item.information_marker_size =
        NormalizeSize(item.information_marker_size);
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
    const bool has_text = has_value || has_description;

    if (HasArea(item.shortcut_size) && has_text) {
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
      cursor_y += item.description_size.height;
    }

    Rect information_rect;
    if (HasArea(item.information_marker_size)) {
      if (HasArea(item.shortcut_size) || has_text) {
        cursor_y += parameters.information_marker_gap;
      }
      information_rect =
          Rect(CenteredLeft(candidate_rect,
                            item.information_marker_size.width),
               cursor_y, item.information_marker_size.width,
               item.information_marker_size.height);
    }

    candidates_.push_back(CandidateGeometry{
        candidate_rect,
        shortcut_rect,
        value_rect,
        description_rect,
        information_rect,
    });

    candidate_right =
        candidate_left - parameters.candidate_gap;
  }

  footer_rect_ =
      Rect(parameters.window_border,
           parameters.window_border + body_height,
           inner_width, parameters.footer_size.height);
}

Rect MacVerticalCandidateLayout::GetCandidateRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].candidate_rect;
}

Rect MacVerticalCandidateLayout::GetShortcutRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].shortcut_rect;
}

Rect MacVerticalCandidateLayout::GetValueRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].value_rect;
}

Rect MacVerticalCandidateLayout::GetDescriptionRect(size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].description_rect;
}

Rect MacVerticalCandidateLayout::GetInformationMarkerRect(
    size_t index) const {
  if (index >= candidates_.size()) {
    return Rect();
  }
  return candidates_[index].information_marker_rect;
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
