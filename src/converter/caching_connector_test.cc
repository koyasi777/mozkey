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

// All rights reserved.

#include "converter/caching_connector.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "converter/connector.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/pos_matcher.h"
#include "engine/modules.h"
#include "testing/gmock.h"
#include "testing/gunit.h"

namespace mozc {
namespace {

class CachingConnectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(
        modules_,
        engine::Modules::Create(std::make_unique<testing::MockDataManager>()));
  }

  const Connector& connector() const { return modules_->GetConnector(); }
  const dictionary::PosMatcher& pos_matcher() const {
    return modules_->GetPosMatcher();
  }

 private:
  std::unique_ptr<const engine::Modules> modules_;
};

TEST_F(CachingConnectorTest, BasicCacheWithoutBonus) {
  CachingConnector<false> conn(connector(), 0, pos_matcher());

  constexpr uint16_t kLid1 = 10;
  constexpr uint16_t kLid2 = 20;
  constexpr uint16_t kRid1 = 5;
  constexpr uint16_t kRid2 = 15;

  conn.ResetCacheIfNecessary(kLid1);
  const int cost1 = conn.GetTransitionCost(kRid1, kLid1);
  EXPECT_EQ(cost1, connector().GetTransitionCost(kRid1, kLid1));

  // Cache hit.
  EXPECT_EQ(conn.GetTransitionCost(kRid1, kLid1), cost1);

  const int cost2 = conn.GetTransitionCost(kRid2, kLid1);
  EXPECT_EQ(cost2, connector().GetTransitionCost(kRid2, kLid1));
  EXPECT_EQ(conn.GetTransitionCost(kRid2, kLid1), cost2);

  // Switch right node LID.
  conn.ResetCacheIfNecessary(kLid2);
  const int cost_lid2 = conn.GetTransitionCost(kRid1, kLid2);
  EXPECT_EQ(cost_lid2, connector().GetTransitionCost(kRid1, kLid2));

  // Switch back to LID1: rid2 is still cached for kLid1!
  conn.ResetCacheIfNecessary(kLid1);
  EXPECT_EQ(conn.GetTransitionCost(kRid2, kLid1), cost2);
  // rid1 was overwritten with kLid2, so it misses and updates for kLid1.
  EXPECT_EQ(conn.GetTransitionCost(kRid1, kLid1), cost1);
}

TEST_F(CachingConnectorTest, LargeRidBeyondCacheSize) {
  CachingConnector<false> conn(connector(), 0, pos_matcher());

  constexpr uint16_t kLargeRid = 2048;  // Equal to kCacheSize
  constexpr uint16_t kLid = 10;

  conn.ResetCacheIfNecessary(kLid);
  const int expected = connector().GetTransitionCost(kLargeRid, kLid);
  EXPECT_EQ(conn.GetTransitionCost(kLargeRid, kLid), expected);
  // Call again: should still return expected without error.
  EXPECT_EQ(conn.GetTransitionCost(kLargeRid, kLid), expected);
}

TEST_F(CachingConnectorTest, WithBonus) {
  constexpr int kBonus = 500;
  CachingConnector<true> conn(connector(), kBonus, pos_matcher());

  // Find a target LID and RID pair where the bonus applies.
  uint16_t target_lid = 0;
  bool found_lid = false;
  for (uint16_t lid = 0; lid < connector().GetResolution(); ++lid) {
    if (pos_matcher().IsContentWordWithConjugation(lid)) {
      target_lid = lid;
      found_lid = true;
      break;
    }
  }

  uint16_t target_rid = 0;
  bool found_rid = false;
  for (uint16_t rid = 0; rid < 2048; ++rid) {
    if (pos_matcher().IsContentNoun(rid) || pos_matcher().IsPronoun(rid)) {
      target_rid = rid;
      found_rid = true;
      break;
    }
  }

  if (found_lid && found_rid) {
    conn.ResetCacheIfNecessary(target_lid);
    const int raw_cost = connector().GetTransitionCost(target_rid, target_lid);
    const int expected_cost = std::max(0, raw_cost - kBonus);
    EXPECT_EQ(conn.GetTransitionCost(target_rid, target_lid), expected_cost);

    // Verify cache hit returns the bonused cost.
    EXPECT_EQ(conn.GetTransitionCost(target_rid, target_lid), expected_cost);
  }
}

}  // namespace
}  // namespace mozc
