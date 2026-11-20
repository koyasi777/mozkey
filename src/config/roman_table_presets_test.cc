#include "config/roman_table_presets.h"

#include <string>

#include "base/hash.h"
#include "gtest/gtest.h"

namespace mozc {
namespace config {
namespace {

TEST(RomanTablePresetsTest, KnownAndUnknownIds) {
  EXPECT_TRUE(GetRomanTablePreset("unknown").empty());
  EXPECT_TRUE(GetRomanTablePreset("").empty());
  EXPECT_GT(GetRomanTablePreset("simplified_azik").size(), 100u);
  EXPECT_GT(GetRomanTablePreset("shingetsu").size(), 100u);
  EXPECT_GT(GetRomanTablePreset("seoto").size(), 100u);
  EXPECT_GT(GetRomanTablePreset("seoto_plus").size(), 100u);
  EXPECT_NE(GetRomanTablePreset("seoto"),
            GetRomanTablePreset("seoto_plus"));
  EXPECT_NE(GetRomanTablePreset("seoto").find("q\t"),
            absl::string_view::npos);
  EXPECT_NE(GetRomanTablePreset("seoto_plus").find("sr\t"),
            absl::string_view::npos);
}

TEST(RomanTablePresetsTest, FollowsLatestIfSnapshotUnmodified) {
  Config config;
  config.set_roman_table_preset_id("seoto_plus");
  config.set_custom_roman_table("old snapshot\n");
  config.set_roman_table_preset_snapshot_hash(
      CityFingerprint(config.custom_roman_table()));
  NormalizeRomanTablePreset(&config);
  EXPECT_EQ(config.roman_table_preset_id(), "seoto_plus");
  const absl::string_view latest = GetRomanTablePreset("seoto_plus");
  EXPECT_EQ(config.custom_roman_table(),
            std::string(latest.data(), latest.size()));
  EXPECT_EQ(config.roman_table_preset_snapshot_hash(),
            CityFingerprint(config.custom_roman_table()));
}

TEST(RomanTablePresetsTest, ExternalCustomEditIsNeverOverwritten) {
  Config config;
  config.set_roman_table_preset_id("seoto_plus");
  config.set_custom_roman_table("edited-by-user\n");
  config.set_roman_table_preset_snapshot_hash(CityFingerprint("old snapshot\n"));
  NormalizeRomanTablePreset(&config);
  EXPECT_TRUE(config.roman_table_preset_id().empty());
  EXPECT_FALSE(config.has_roman_table_preset_snapshot_hash());
  EXPECT_EQ(config.custom_roman_table(), "edited-by-user\n");
}

TEST(RomanTablePresetsTest, UnknownIdKeepsLastSnapshot) {
  Config config;
  config.set_roman_table_preset_id("removed_preset");
  config.set_custom_roman_table("last-known-table\n");
  config.set_roman_table_preset_snapshot_hash(
      CityFingerprint(config.custom_roman_table()));
  NormalizeRomanTablePreset(&config);
  EXPECT_TRUE(config.roman_table_preset_id().empty());
  EXPECT_EQ(config.custom_roman_table(), "last-known-table\n");
}

TEST(RomanTablePresetsTest, UserPresetResolvesAndRetainsData) {
  Config c;
  auto* item = c.add_user_roman_table_presets();
  item->set_id("a-uuid");
  item->set_name("My layout");
  item->set_table("ka\tか\n");
  c.set_roman_table_preset_id("user:a-uuid");
  c.set_custom_roman_table(item->table());
  c.set_roman_table_preset_snapshot_hash(CityFingerprint(c.custom_roman_table()));
  NormalizeRomanTablePreset(&c);
  EXPECT_EQ(c.roman_table_preset_id(), "user:a-uuid");
  EXPECT_EQ(c.custom_roman_table(), item->table());
}

TEST(RomanTablePresetsTest, UpdatingUserPresetFollowsWhenUnmodified) {
  Config c;
  auto* item = c.add_user_roman_table_presets();
  item->set_id("uuid");
  item->set_table("ka\tか\n");
  c.set_roman_table_preset_id("user:uuid");
  c.set_custom_roman_table(item->table());
  c.set_roman_table_preset_snapshot_hash(CityFingerprint(c.custom_roman_table()));
  item->set_table("ka\tか\nki\tき\n");
  NormalizeRomanTablePreset(&c);
  EXPECT_EQ(c.custom_roman_table(), "ka\tか\nki\tき\n");
}

TEST(RomanTablePresetsTest, DeletingActivePresetRetainsCustomRules) {
  Config c;
  c.set_roman_table_preset_id("user:deleted");
  c.set_custom_roman_table("ka\tか\n");
  c.set_roman_table_preset_snapshot_hash(CityFingerprint(c.custom_roman_table()));
  NormalizeRomanTablePreset(&c);
  EXPECT_TRUE(c.roman_table_preset_id().empty());
  EXPECT_EQ(c.custom_roman_table(), "ka\tか\n");
}

TEST(RomanTablePresetsTest, ExternalUserEditDoesNotFollow) {
  Config c;
  auto* item = c.add_user_roman_table_presets();
  item->set_id("uuid");
  item->set_table("ki\tき\n");
  c.set_roman_table_preset_id("user:uuid");
  c.set_custom_roman_table("edited\t規則\n");
  c.set_roman_table_preset_snapshot_hash(CityFingerprint("ki\tき\n"));
  NormalizeRomanTablePreset(&c);
  EXPECT_TRUE(c.roman_table_preset_id().empty());
  EXPECT_EQ(c.custom_roman_table(), "edited\t規則\n");
}

TEST(RomanTablePresetsTest, PublishedSeotoPlusCorrections) {
  const absl::string_view plus = GetRomanTablePreset("seoto_plus");
  EXPECT_NE(plus.find("vz\t↖\n"), absl::string_view::npos);
  EXPECT_EQ(plus.find("vz\t↘\n"), absl::string_view::npos);
  EXPECT_NE(plus.find(
      "ctnnrmsnd\tことになりませんので\t\tDisplayAmbiguousResult\n"),
      absl::string_view::npos);
  EXPECT_EQ(plus.find(
      "ctnnrmsnd\tことになりませんので \t\tDisplayAmbiguousResult\n"),
      absl::string_view::npos);
}

}  // namespace
}  // namespace config
}  // namespace mozc
