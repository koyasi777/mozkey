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

#ifndef MOZC_CONVERTER_CACHING_CONNECTOR_H_
#define MOZC_CONVERTER_CACHING_CONNECTOR_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

#include "absl/log/check.h"
#include "converter/connector.h"
#include "dictionary/pos_matcher.h"

namespace mozc {

// A wrapper for Connector to minimize calls of Connector::GetTransitionCost()
// in Viterbi algorithm. This way the performance of Viterbi algorithm improves
// significantly in terms of time consumption because
// Connector::GetTransitionCost() is slow due to its compression format. This
// class implements a cache strategy designed for the access pattern in Viterbi
// algorithm.
//
// In Viterbi algorithm, the connection matrix is looked up in a nested loop as
// follows:
//
// for each right node `r`:
//   for each left node `l`:
//     ...
//     transition cost = connector.GetTransitionCost(l.rid, r.lid)
//     ...
//
// Therefore, in the inner loop, `r.lid` is fixed. We cache the transition cost
// for (l.rid, r.lid) in `cache[l.rid]`, tagged with `r.lid`. This tag-based
// cache avoids clearing the entire cache array when `r.lid` changes, completely
// eliminating memset overhead during Viterbi DP, while allowing cached entries
// to be reused when the same `r.lid` appears again.
//
// NOTE: This class is designed for Viterbi or A* search algorithms (which have
// a similar nested loop access pattern) and won't work for other purposes.
//
// This class also centrally manages the application of the experimental
// transition cost bonus (e.g., for particle omission). While there are other
// places in the decoder that directly call `Connector::GetTransitionCost` (such
// as personal name processing or compound splitting), they do not need to apply
// this bonus because they only handle POS pairs where the bonus is never
// applicable (e.g., noun-to-noun) or such transitions do not occur in practice.
// Thus, applying the bonus only within this class is sufficient to maintain
// cost consistency.
//
// The template parameter `kHasBonus` controls whether the transition cost bonus
// is applied. `if constexpr` is used internally to eliminate runtime overhead
// when the bonus is disabled.
template <bool kHasBonus>
class CachingConnector final {
 public:
  CachingConnector(const Connector& connector,
                   int particle_omission_transition_cost_bonus,
                   const dictionary::PosMatcher& pos_matcher)
      : connector_(connector),
        particle_omission_transition_cost_bonus_(
            particle_omission_transition_cost_bonus),
        pos_matcher_(pos_matcher) {}

  CachingConnector(const CachingConnector&) = delete;
  CachingConnector& operator=(const CachingConnector&) = delete;

  // Prepares the cache for the inner loop where `rnode_lid` is fixed.
  // When bonus is enabled, precomputes whether the right node is a bonus
  // target.
  void ResetCacheIfNecessary(uint16_t rnode_lid) {
    if (cache_lid_ != rnode_lid) {
      cache_lid_ = rnode_lid;
      if constexpr (kHasBonus) {
        is_right_node_target_ =
            (particle_omission_transition_cost_bonus_ > 0) &&
            pos_matcher_.IsContentWordWithConjugation(rnode_lid);
      }
    }
  }

  int GetTransitionCost(uint16_t lnode_rid, uint16_t rnode_lid) {
    DCHECK_EQ(cache_lid_, rnode_lid);
    // Values for rid >= kCacheSize cannot be cached. However, frequent PoSs
    // have smaller IDs, so caching only for rid in [0, kCacheSize) works well.
    if (lnode_rid >= kCacheSize) {
      const int cost = connector_.GetTransitionCost(lnode_rid, rnode_lid);
      if constexpr (kHasBonus) {
        return ApplyBonus(cost, lnode_rid);
      } else {
        return cost;
      }
    }

    const CacheEntry& entry = cache_[lnode_rid];
    if (entry.lid == rnode_lid) {
      return entry.cost;
    }

    int cost = connector_.GetTransitionCost(lnode_rid, rnode_lid);
    if constexpr (kHasBonus) {
      cost = ApplyBonus(cost, lnode_rid);
    }
    cache_[lnode_rid] = {rnode_lid, cost};
    return cost;
  }

 private:
  int ApplyBonus(int cost, uint16_t lnode_rid) const {
    if constexpr (kHasBonus) {
      if (is_right_node_target_ && (pos_matcher_.IsContentNoun(lnode_rid) ||
                                    pos_matcher_.IsPronoun(lnode_rid))) {
        return std::max(0, cost - particle_omission_transition_cost_bonus_);
      }
    }
    return cost;
  }

  static constexpr uint16_t kInvalidLid = std::numeric_limits<uint16_t>::max();
  static constexpr int kCacheSize = 2048;

  struct CacheEntry {
    uint16_t lid = kInvalidLid;
    int cost = 0;
  };

  const Connector& connector_;
  const int particle_omission_transition_cost_bonus_;
  const dictionary::PosMatcher& pos_matcher_;
  std::array<CacheEntry, kCacheSize> cache_{};
  uint16_t cache_lid_ = kInvalidLid;
  bool is_right_node_target_ = false;
};

}  // namespace mozc

#endif  // MOZC_CONVERTER_CACHING_CONNECTOR_H_
