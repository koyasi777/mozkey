#ifndef MOZC_RENDERER_CONVERSION_STATUS_INDICATOR_H_
#define MOZC_RENDERER_CONVERSION_STATUS_INDICATOR_H_

#include <algorithm>
#include <array>
#include <cstdint>

#include "base/coordinates.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"

namespace mozc::renderer {

// This is the DISPLAY status, not a per-character provenance claim.
enum class ConversionDisplayStatus { kHidden, kReading, kMozc, kZenzPending, kZenz };

inline ConversionDisplayStatus ResolveConversionDisplayStatus(
    const commands::Output& output) {
  if (!output.has_preedit() || output.preedit().segment_size() == 0) {
    return ConversionDisplayStatus::kHidden;
  }
  if (output.zenz_live_correction_pending()) {
    return ConversionDisplayStatus::kZenzPending;
  }
  if (output.zenz_live_correction_applied()) {
    return ConversionDisplayStatus::kZenz;
  }
  if (output.live_conversion()) {
    return ConversionDisplayStatus::kMozc;
  }
  // Before live conversion starts, or when the user is composing normally,
  // there is no materialized Mozc/Zenz conversion to attribute.
  return ConversionDisplayStatus::kReading;
}

// Unfocused suggestions are companions to ongoing composition, whether or
// not live conversion has started. Hide only when navigating a focused
// suggestion, conversion, or prediction list.
inline bool ShouldHideConversionStatusForCandidate(
    const commands::Output& output, bool candidate_window_visible) {
  if (!candidate_window_visible) return false;
  if (output.has_candidate_window()) {
    const auto& candidate = output.candidate_window();
    if (candidate.has_category() &&
        candidate.category() == commands::SUGGESTION &&
        candidate.candidate_size() > 0 &&
        !candidate.has_focused_index()) {
      return false;
    }
  }
  return true;
}

inline uint32_t ConversionStatusColor(
    ConversionDisplayStatus status,
    const config::Config::ConversionStatusIndicatorStyle& style) {
  switch (status) {
    case ConversionDisplayStatus::kMozc: return style.mozc_color();
    case ConversionDisplayStatus::kZenzPending: return style.zenz_pending_color();
    case ConversionDisplayStatus::kZenz: return style.zenz_color();
    case ConversionDisplayStatus::kReading: return style.reading_color();
    case ConversionDisplayStatus::kHidden: return style.reading_color();
  }
  return style.reading_color();
}

// Reduce only the displayed full-name label in vertical writing.  Circle
// badges render a symbol even when FULL_LABEL is configured.  Font size remains
// a user preference in points; 2 is a logical-pixel (DIP) rendering adjustment.
inline int ConversionStatusVerticalFullLabelFontReduction(
    const config::Config::ConversionStatusIndicatorStyle& style,
    bool vertical) {
  return vertical &&
                 style.label() == config::Config::CONVERSION_STATUS_FULL_LABEL &&
                 style.shape() != config::Config::CONVERSION_STATUS_CIRCLE
             ? 2
             : 0;
}

// All rectangles use the Mozc top-left screen coordinate system. preedit is
// the local anchor; full_preedit is the optional host-verified bounds of the
// *entire* composition. Separating them keeps the caret/suggestion anchor
// stable while preventing overlap with other composing characters.
// A requested placement is a preference, not a reason to hide at a screen edge.
inline bool PlaceConversionStatusIndicator(
    const Rect& preedit, const Rect* ruby, const Rect* obstacle,
    const Rect& work, const Size& size, bool vertical,
    const config::Config::ConversionStatusIndicatorStyle& style, Rect* result,
    const Rect* full_preedit = nullptr) {
  if (result == nullptr || work.Width() <= 0 || work.Height() <= 0 ||
      size.width <= 0 || size.height <= 0 ||
      size.width > work.Width() || size.height > work.Height()) {
    return false;
  }
  // Missing or malformed full geometry must not move the anchor.
  // It also must not authorize a side-of-preedit fallback in AUTO mode: the
  // caret rectangle is not the right edge of a multi-character composition.
  const bool has_full_preedit = full_preedit != nullptr &&
      full_preedit->Width() > 0 && full_preedit->Height() > 0;
  const Rect& exclusion = has_full_preedit ? *full_preedit : preedit;
  const int gap = std::clamp(static_cast<int>(style.gap()), 0, 60);
  const int dx = std::clamp(style.offset_x(), -100, 100);
  const int dy = std::clamp(style.offset_y(), -100, 100);
  const auto intersects = [](const Rect& a, const Rect& b) {
    return a.Left() < b.Right() && b.Left() < a.Right() &&
           a.Top() < b.Bottom() && b.Top() < a.Bottom();
  };
  const auto check = [&](int x, int y) {
    Rect rect(x + dx, y + dy, size.width, size.height);
    if (rect.Left() < work.Left() || rect.Right() > work.Right() ||
        rect.Top() < work.Top() || rect.Bottom() > work.Bottom() ||
        intersects(rect, preedit) || intersects(rect, exclusion) ||
        (ruby != nullptr && intersects(rect, *ruby)) ||
        (obstacle != nullptr && intersects(rect, *obstacle))) {
      return false;
    }
    *result = rect;
    return true;
  };
  const auto above_preedit = [&]() {
    return check(preedit.Left(), exclusion.Top() - size.height - gap);
  };
  const auto below_preedit = [&]() {
    return check(preedit.Left(), exclusion.Bottom() + gap);
  };
  // Center the indicator *only when it follows the ruby reading axis*:
  // horizontally beside the ruby in horizontal writing, or below the ruby
  // in vertical writing.  Other placements retain their leading-edge
  // anchors (including ruby-above and all preedit-based placements).
  // check() applies the user-configured x/y offsets after this alignment.
  const auto centered_x_on_ruby = [&]() {
    return ruby->Left() + (ruby->Width() - size.width) / 2;
  };
  const auto centered_y_on_ruby = [&]() {
    return ruby->Top() + (ruby->Height() - size.height) / 2;
  };
  // In vertical writing, the reading direction flows downward within a
  // column.  Thus "beside ruby" follows the ruby column (below it), while
  // "above ruby" means outside the ruby column (right, then left).
  const auto ruby_side = [&]() {
    if (!ruby) return false;
    if (vertical) {
      return check(centered_x_on_ruby(), ruby->Bottom() + gap);
    }
    return check(ruby->Right() + gap, centered_y_on_ruby()) ||
           check(ruby->Left() - size.width - gap, centered_y_on_ruby());
  };
  const auto ruby_above = [&]() {
    if (!ruby) return false;
    if (vertical) {
      return check(ruby->Right() + gap, ruby->Top()) ||
             check(ruby->Left() - size.width - gap, ruby->Top());
    }
    return check(ruby->Left(), ruby->Top() - size.height - gap);
  };
  const auto preedit_side = [&]() {
    return check(exclusion.Right() + gap, preedit.Top()) ||
           check(exclusion.Left() - size.width - gap, preedit.Top());
  };
  // After both above/below placements fail, try beside the visible passive
  // suggestion. This uses the obstacle bounds already provided by each OS;
  // check() also rejects overlap with ruby and any known full preedit bounds.
  const auto suggestion_side = [&]() {
    return obstacle != nullptr &&
           (check(obstacle->Right() + gap, obstacle->Top()) ||
            check(obstacle->Left() - size.width - gap, obstacle->Top()));
  };
  // Keep same-row obstacle avoidance first; only when it fails may an
  // explicitly preferred side flip across the entire composition.
  const auto on_requested_row = [&](int x, int y) {
    if (check(x, y)) return true;
    if (ruby && (check(ruby->Right() + gap, y) ||
                 check(ruby->Left() - size.width - gap, y))) {
      return true;
    }
    return obstacle &&
           (check(obstacle->Right() + gap, y) ||
            check(obstacle->Left() - size.width - gap, y));
  };
  switch (style.placement()) {
    case config::Config::CONVERSION_STATUS_RUBY_SIDE:
      return ruby ? ruby_side()
                  : (vertical ? below_preedit() : preedit_side());
    case config::Config::CONVERSION_STATUS_RUBY_ABOVE:
      if (vertical) return ruby ? ruby_above() : preedit_side();
      return (ruby ? on_requested_row(ruby->Left(),
                          ruby->Top() - size.height - gap)
                  : on_requested_row(preedit.Left(),
                          exclusion.Top() - size.height - gap)) ||
             below_preedit();
    case config::Config::CONVERSION_STATUS_PREEDIT_ABOVE:
      return on_requested_row(preedit.Left(),
                              exclusion.Top() - size.height - gap) ||
             on_requested_row(preedit.Left(), exclusion.Bottom() + gap) ||
             (has_full_preedit && preedit_side());
    case config::Config::CONVERSION_STATUS_PREEDIT_BELOW:
      return on_requested_row(preedit.Left(), exclusion.Bottom() + gap) ||
             on_requested_row(preedit.Left(),
                              exclusion.Top() - size.height - gap) ||
             (has_full_preedit && preedit_side());
    case config::Config::CONVERSION_STATUS_AUTO:
      if (ruby != nullptr && (ruby_side() || ruby_above())) return true;
      if (vertical && preedit_side()) return true;
      if (above_preedit()) return true;
      if (below_preedit()) return true;
      if (suggestion_side()) return true;
      return has_full_preedit && preedit_side();
  }
  return false;
}

}  // namespace mozc::renderer

#endif  // MOZC_RENDERER_CONVERSION_STATUS_INDICATOR_H_
