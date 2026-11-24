// Copyright 2026, Mozkey contributors.
#include "engine/dictionary_cost_response.h"

#include <cstddef>
#include <string>
#include <utility>

#include "dictionary/dictionary_interface.h"
#include "protocol/commands.pb.h"
#include "protocol/dictionary_cost_limits.h"
#include "testing/gunit.h"

namespace mozc::engine::internal {
namespace {

using Source = dictionary::DictionaryCostEntry::Source;

void AddEntry(dictionary::DictionaryCostLookupResult* result, Source source,
              std::string value) {
  dictionary::Token token;
  token.key = "てすと";
  token.value = std::move(value);
  token.cost = 4200;
  token.lid = 12;
  token.rid = 13;
  result->entries.push_back({source, std::move(token)});
}

bool HasValue(const commands::DictionaryCostLookupResult& result,
              const std::string& value) {
  for (const auto& entry : result.entries()) {
    if (entry.value() == value) {
      return true;
    }
  }
  return false;
}

TEST(DictionaryCostResponseTest, OversizedSystemDoesNotHideShortUserWord) {
  dictionary::DictionaryCostLookupResult raw;
  AddEntry(&raw, Source::kSystem, std::string(300 * 1024, 's'));
  AddEntry(&raw, Source::kSystem, "短いシステム語");
  AddEntry(&raw, Source::kUser, "利用者語");

  commands::DictionaryCostLookupResult response;
  response.add_entries()->set_value("old value");
  BuildBoundedDictionaryCostResponse(raw, &response);

  ASSERT_EQ(response.entries_size(), 2);
  EXPECT_EQ(response.entries(0).source(), commands::DictionaryCostEntry::SYSTEM);
  EXPECT_EQ(response.entries(0).value(), "短いシステム語");
  EXPECT_EQ(response.entries(1).source(), commands::DictionaryCostEntry::USER);
  EXPECT_EQ(response.entries(1).value(), "利用者語");
  EXPECT_TRUE(response.system_truncated());
  EXPECT_FALSE(response.user_truncated());
  EXPECT_LE(response.ByteSizeLong(), commands::kDictionaryCostMaxResponseBytes);
}

TEST(DictionaryCostResponseTest, BothSourcesStayIndependentAtHalfBudget) {
  dictionary::DictionaryCostLookupResult raw;
  AddEntry(&raw, Source::kSystem, std::string(180 * 1024, 's'));
  AddEntry(&raw, Source::kSystem, "短いシステム語");
  AddEntry(&raw, Source::kUser, std::string(180 * 1024, 'u'));
  AddEntry(&raw, Source::kUser, "短いユーザー語");

  commands::DictionaryCostLookupResult response;
  BuildBoundedDictionaryCostResponse(raw, &response);
  // The two short entries fit in their respective protected shares. The
  // second pass can then borrow the remaining budget for the first 180 KiB
  // entry (system). The second 180 KiB entry (user) does not fit as well.
  ASSERT_EQ(response.entries_size(), 3);
  EXPECT_TRUE(HasValue(response, "短いシステム語"));
  EXPECT_TRUE(HasValue(response, "短いユーザー語"));
  EXPECT_TRUE(HasValue(response, std::string(180 * 1024, 's')));
  EXPECT_FALSE(HasValue(response, std::string(180 * 1024, 'u')));
  EXPECT_FALSE(response.system_truncated());
  EXPECT_TRUE(response.user_truncated());
  EXPECT_EQ(response.entries(0).source(), commands::DictionaryCostEntry::SYSTEM);
  EXPECT_EQ(response.entries(1).source(), commands::DictionaryCostEntry::SYSTEM);
  EXPECT_EQ(response.entries(2).source(), commands::DictionaryCostEntry::USER);
  EXPECT_LE(response.ByteSizeLong(), commands::kDictionaryCostMaxResponseBytes);
}

TEST(DictionaryCostResponseTest, OneSourceMayUseEntireBudget) {
  dictionary::DictionaryCostLookupResult raw;
  AddEntry(&raw, Source::kSystem, std::string(180 * 1024, 's'));

  commands::DictionaryCostLookupResult response;
  BuildBoundedDictionaryCostResponse(raw, &response);
  ASSERT_EQ(response.entries_size(), 1);
  EXPECT_FALSE(response.system_truncated());
  EXPECT_FALSE(response.user_truncated());
  EXPECT_LE(response.ByteSizeLong(), commands::kDictionaryCostMaxResponseBytes);
}

TEST(DictionaryCostResponseTest, UnusedReservationIsAvailableToOtherSource) {
  dictionary::DictionaryCostLookupResult raw;
  AddEntry(&raw, Source::kSystem, std::string(200 * 1024, 's'));
  AddEntry(&raw, Source::kUser, "利用者語");

  commands::DictionaryCostLookupResult response;
  BuildBoundedDictionaryCostResponse(raw, &response);
  ASSERT_EQ(response.entries_size(), 2);
  EXPECT_EQ(response.entries(1).value(), "利用者語");
  EXPECT_FALSE(response.system_truncated());
  EXPECT_FALSE(response.user_truncated());
  EXPECT_LE(response.ByteSizeLong(), commands::kDictionaryCostMaxResponseBytes);
}

TEST(DictionaryCostResponseTest, TruncationFlagsAndBoundsRemainConsistent) {
  dictionary::DictionaryCostLookupResult raw;
  raw.user_truncated = true;  // Source-side limit was reached before packing.
  for (int i = 0; i < 12; ++i) {
    AddEntry(&raw, Source::kSystem, std::string(50 * 1024, 's'));
    AddEntry(&raw, Source::kUser, std::string(40 * 1024, 'u'));
  }
  AddEntry(&raw, Source::kUser, "末尾の短い語");

  commands::DictionaryCostLookupResult response;
  BuildBoundedDictionaryCostResponse(raw, &response);
  EXPECT_TRUE(response.system_truncated());
  EXPECT_TRUE(response.user_truncated());
  EXPECT_TRUE(HasValue(response, "末尾の短い語"));
  EXPECT_LE(response.ByteSizeLong(), commands::kDictionaryCostMaxResponseBytes);
}


TEST(DictionaryCostResponseTest, ReclaimsCapacityFromUnrepresentableSystemWord) {
  dictionary::DictionaryCostLookupResult raw;
  // The system entry cannot fit the 256 KiB response at all, so it must not
  // consume a reserved half and hide an otherwise representable user entry.
  AddEntry(&raw, Source::kSystem, std::string(300 * 1024, 's'));
  AddEntry(&raw, Source::kUser, std::string(180 * 1024, 'u'));

  commands::DictionaryCostLookupResult response;
  BuildBoundedDictionaryCostResponse(raw, &response);
  ASSERT_EQ(response.entries_size(), 1);
  EXPECT_EQ(response.entries(0).source(), commands::DictionaryCostEntry::USER);
  EXPECT_EQ(response.entries(0).value().size(), 180u * 1024u);
  EXPECT_TRUE(response.system_truncated());
  EXPECT_FALSE(response.user_truncated());
  EXPECT_LE(response.ByteSizeLong(), commands::kDictionaryCostMaxResponseBytes);
}

TEST(DictionaryCostResponseTest, ReclaimsHalfWhenNoSystemWordFits) {
  dictionary::DictionaryCostLookupResult raw;
  AddEntry(&raw, Source::kSystem, std::string(300 * 1024, 's'));
  AddEntry(&raw, Source::kUser, std::string(170 * 1024, 'u'));
  AddEntry(&raw, Source::kUser, "短いユーザー語");

  commands::DictionaryCostLookupResult response;
  BuildBoundedDictionaryCostResponse(raw, &response);
  ASSERT_EQ(response.entries_size(), 2);
  EXPECT_EQ(response.entries(0).source(), commands::DictionaryCostEntry::USER);
  EXPECT_EQ(response.entries(1).value(), "短いユーザー語");
  EXPECT_TRUE(response.system_truncated());
  EXPECT_FALSE(response.user_truncated());
  EXPECT_LE(response.ByteSizeLong(), commands::kDictionaryCostMaxResponseBytes);
}

}  // namespace
}  // namespace mozc::engine::internal
