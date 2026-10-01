#include "session/zenz_local_correction_store.h"

#include <string>
#include <vector>

#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mozc::session {
namespace {

#if defined(_WIN32)

std::wstring JoinPathForTest(
    const std::wstring& lhs, const std::wstring& rhs) {
  if (lhs.empty()) {
    return rhs;
  }
  return lhs.back() == L'\\' ? lhs + rhs : lhs + L"\\" + rhs;
}

bool EnsureDirectoryForTest(const std::wstring& path) {
  if (::CreateDirectoryW(path.c_str(), nullptr)) {
    return true;
  }
  return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

class ScopedProfileForLocalCorrectionTest {
 public:
  ScopedProfileForLocalCorrectionTest() {
    wchar_t old_profile[32767] = {};
    const DWORD old_len =
        ::GetEnvironmentVariableW(L"USERPROFILE", old_profile, 32767);
    if (old_len > 0 && old_len < 32767) {
      has_old_profile_ = true;
      old_profile_.assign(old_profile, old_len);
    }

    wchar_t temp_path[MAX_PATH] = {};
    const DWORD temp_len = ::GetTempPathW(MAX_PATH, temp_path);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
      return;
    }

    profile_dir_ =
        std::wstring(temp_path, temp_len) +
        L"mozc_zenz_local_correction_store_test_" +
        std::to_wstring(::GetCurrentProcessId()) + L"_" +
        std::to_wstring(::GetTickCount64());

    const std::wstring app_data =
        JoinPathForTest(profile_dir_, L"AppData");
    const std::wstring local_low =
        JoinPathForTest(app_data, L"LocalLow");

    if (!EnsureDirectoryForTest(profile_dir_) ||
        !EnsureDirectoryForTest(app_data) ||
        !EnsureDirectoryForTest(local_low)) {
      return;
    }

    ok_ =
        ::SetEnvironmentVariableW(L"USERPROFILE", profile_dir_.c_str());
  }

  ~ScopedProfileForLocalCorrectionTest() {
    if (has_old_profile_) {
      ::SetEnvironmentVariableW(L"USERPROFILE", old_profile_.c_str());
    } else {
      ::SetEnvironmentVariableW(L"USERPROFILE", nullptr);
    }

    const std::wstring app_data =
        JoinPathForTest(profile_dir_, L"AppData");
    const std::wstring local_low =
        JoinPathForTest(app_data, L"LocalLow");
    const std::wstring mozc =
        JoinPathForTest(local_low, L"Mozc");
    const std::wstring store =
        JoinPathForTest(mozc, L"zenz_local_corrections.tsv");

    ::DeleteFileW(store.c_str());
    ::RemoveDirectoryW(mozc.c_str());
    ::RemoveDirectoryW(local_low.c_str());
    ::RemoveDirectoryW(app_data.c_str());
    ::RemoveDirectoryW(profile_dir_.c_str());
  }

  bool ok() const { return ok_; }

 private:
  bool ok_ = false;
  bool has_old_profile_ = false;
  std::wstring old_profile_;
  std::wstring profile_dir_;
};

#elif defined(__APPLE__)

class ScopedProfileForLocalCorrectionTest {
 public:
  ScopedProfileForLocalCorrectionTest()
      : old_profile_dir_(SystemUtil::GetUserProfileDirectory()),
        profile_dir_(mozc::testing::MakeTempDirectoryOrDie()) {
    ok_ = !profile_dir_.path().empty();
    if (ok_) {
      SystemUtil::SetUserProfileDirectory(profile_dir_.path());
    }
  }

  ~ScopedProfileForLocalCorrectionTest() {
    SystemUtil::SetUserProfileDirectory(old_profile_dir_);
  }

  bool ok() const { return ok_; }

 private:
  bool ok_ = false;
  std::string old_profile_dir_;
  TempDirectory profile_dir_;
};

#endif

#if defined(_WIN32) || defined(__APPLE__)

TEST(ZenzLocalCorrectionStoreTest, RecordsAndLoadsExactCorrection) {
  ScopedProfileForLocalCorrectionTest profile;
  ASSERT_TRUE(profile.ok());

  {
    ZenzLocalCorrectionStore store;
    store.RecordCorrection(
        "みこみっと", "ミコミット", "未コミット");
    store.RecordCorrection(
        "みこみっと", "ミコミット", "未コミット");

    const auto found =
        store.Lookup("みこみっと", "ミコミット", "未コミット");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->accepted_value, "未コミット");
    EXPECT_EQ(found->observation_count, 2);
  }

  // A new instance proves persistence rather than only in-memory reuse.
  {
    ZenzLocalCorrectionStore store;
    const auto found =
        store.Lookup("みこみっと", "ミコミット", "未コミット");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->accepted_value, "未コミット");
    EXPECT_EQ(found->observation_count, 2);
  }
}

TEST(ZenzLocalCorrectionStoreTest,
     DifferentAcceptedBaselinesCoexistAsExactTriples) {
  ScopedProfileForLocalCorrectionTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzLocalCorrectionStore store;
  store.RecordCorrection(
      "みこみっと", "ミコミット", "未コミット");
  store.RecordCorrection(
      "みこみっと", "ミコミット", "見コミット");

  const auto first =
      store.Lookup("みこみっと", "ミコミット", "未コミット");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->observation_count, 1);

  const auto second =
      store.Lookup("みこみっと", "ミコミット", "見コミット");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->observation_count, 1);

  EXPECT_FALSE(
      store.Lookup("みこみっと", "ミコミット", "味コミット")
          .has_value());

  const std::vector<ZenzLocalCorrectionEntry> entries =
      store.ListEntries();
  ASSERT_EQ(entries.size(), 2);
}

TEST(ZenzLocalCorrectionStoreTest,
     MultipleStoreInstancesObserveEachOthersWrites) {
  ScopedProfileForLocalCorrectionTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzLocalCorrectionStore first;
  ZenzLocalCorrectionStore second;

  first.RecordCorrection(
      "みこみっと", "ミコミット", "未コミット");
  ASSERT_TRUE(
      second.Lookup("みこみっと", "ミコミット", "未コミット")
          .has_value());

  second.RecordCorrection(
      "ほぞん", "ホゾン", "保存");
  ASSERT_TRUE(
      first.Lookup("ほぞん", "ホゾン", "保存").has_value());

  const std::vector<ZenzLocalCorrectionEntry> entries =
      first.ListEntries();
  EXPECT_EQ(entries.size(), 2);
}

TEST(ZenzLocalCorrectionStoreTest,
     DifferentRejectedSurfaceIsIndependent) {
  ScopedProfileForLocalCorrectionTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzLocalCorrectionStore store;
  store.RecordCorrection(
      "みこみっと", "ミコミット", "未コミット");
  store.RecordCorrection(
      "みこみっと", "見コミット", "未コミット");

  ASSERT_TRUE(
      store.Lookup("みこみっと", "ミコミット", "未コミット")
          .has_value());
  ASSERT_TRUE(
      store.Lookup("みこみっと", "見コミット", "未コミット")
          .has_value());
}

TEST(ZenzLocalCorrectionStoreTest,
     DeleteEntryRemovesOnlySelectedExactTriple) {
  ScopedProfileForLocalCorrectionTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzLocalCorrectionStore store;
  store.RecordCorrection(
      "みこみっと", "ミコミット", "未コミット");
  store.RecordCorrection(
      "みこみっと", "ミコミット", "未コミット");
  store.RecordCorrection(
      "みこみっと", "見コミット", "未コミット");

  ASSERT_TRUE(store.DeleteEntry(
      "みこみっと", "ミコミット", "未コミット"));

  EXPECT_FALSE(
      store.Lookup("みこみっと", "ミコミット", "未コミット")
          .has_value());
  ASSERT_TRUE(
      store.Lookup("みこみっと", "見コミット", "未コミット")
          .has_value());

  const std::vector<ZenzLocalCorrectionEntry> entries =
      store.ListEntries();
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].rejected_value, "見コミット");

  // Deleting a stale/nonexistent selection must fail without changing data.
  EXPECT_FALSE(store.DeleteEntry(
      "みこみっと", "ミコミット", "未コミット"));
  EXPECT_EQ(store.ListEntries().size(), 1);
}

TEST(ZenzLocalCorrectionStoreTest,
     DeleteEntryRemovesOnlyOneAcceptedBaselineAlternative) {
  ScopedProfileForLocalCorrectionTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzLocalCorrectionStore store;
  store.RecordCorrection(
      "みこみっと", "ミコミット", "未コミット");
  store.RecordCorrection(
      "みこみっと", "ミコミット", "見コミット");

  ASSERT_TRUE(
      store.Lookup("みこみっと", "ミコミット", "未コミット")
          .has_value());
  ASSERT_TRUE(
      store.Lookup("みこみっと", "ミコミット", "見コミット")
          .has_value());

  ASSERT_TRUE(store.DeleteEntry(
      "みこみっと", "ミコミット", "見コミット"));

  const auto found =
      store.Lookup("みこみっと", "ミコミット", "未コミット");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->accepted_value, "未コミット");
  EXPECT_EQ(found->observation_count, 1);
  EXPECT_FALSE(
      store.Lookup("みこみっと", "ミコミット", "見コミット")
          .has_value());
}

TEST(ZenzLocalCorrectionStoreTest, ClearAllRemovesPersistence) {
  ScopedProfileForLocalCorrectionTest profile;
  ASSERT_TRUE(profile.ok());

  ZenzLocalCorrectionStore store;
  store.RecordCorrection(
      "みこみっと", "ミコミット", "未コミット");
  ASSERT_TRUE(
      store.Lookup("みこみっと", "ミコミット", "未コミット")
          .has_value());

  ASSERT_TRUE(store.ClearAll());
  EXPECT_FALSE(
      store.Lookup("みこみっと", "ミコミット", "未コミット")
          .has_value());

  ZenzLocalCorrectionStore reloaded;
  EXPECT_FALSE(
      reloaded.Lookup("みこみっと", "ミコミット", "未コミット")
          .has_value());
}

#endif

}  // namespace
}  // namespace mozc::session