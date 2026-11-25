// Copyright 2026 Mozkey contributors.
#include "dictionary/external_dictionary_manager.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "base/file_util.h"
#include "base/file/temp_dir.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::dictionary {
namespace {

class ExternalDictionaryManagerTest : public ::testing::Test {
 protected:
  std::string File(const std::string& name) const {
    return FileUtil::JoinPath(directory_.path(), name);
  }

  void Write(const std::string& path, const std::string& data) {
    ASSERT_TRUE(FileUtil::SetContents(path, data).ok());
  }

  TempDirectory directory_ = testing::MakeTempDirectoryOrDie();
};

TEST_F(ExternalDictionaryManagerTest, DisabledByDefaultAndPathSwitchClears) {
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kDisabled);
  EXPECT_TRUE(manager.LookupExact("001").empty());

  const std::string first = File("one.tsv");
  const std::string second = File("two.tsv");
  Write(first, "001\tFirst\n");
  Write(second, "002\tSecond\n");
  manager.SetPath(first);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  ASSERT_EQ(manager.LookupExact("001"), std::vector<std::string>{"First"});

  manager.SetPath(second);
  EXPECT_TRUE(manager.LookupExact("001").empty());
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kLoading);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("002"), std::vector<std::string>{"Second"});

  manager.SetPath("");
  EXPECT_TRUE(manager.LookupExact("002").empty());
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kDisabled);
}

TEST_F(ExternalDictionaryManagerTest, Utf8CrLfBomAndRepeatedKeys) {
  const std::string path = File("entries.tsv");
  Write(path, "\xef\xbb\xbf# comment\r\n"
              "001234\t株式会社山田商事\r\n"
              "001234\t山田商事\r\n"
              "001234\t株式会社山田商事\r\n"
              "12\t田中太郎\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("001234"),
            (std::vector<std::string>{"株式会社山田商事", "山田商事"}));
  EXPECT_TRUE(manager.LookupExact("1234").empty());
  EXPECT_EQ(manager.LookupExact("12"),
            std::vector<std::string>{"田中太郎"});
  EXPECT_EQ(manager.GetStatus().entry_count, 3);
  EXPECT_EQ(manager.GetStatus().revision, 1);
}

TEST_F(ExternalDictionaryManagerTest, ReloadKeepsOldImmutableSnapshotAlive) {
  const std::string path = File("atomic.tsv");
  Write(path, "100\tOriginal\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  const auto old_snapshot = manager.GetSnapshot();
  EXPECT_EQ(old_snapshot->entries.at("100").at(0), "Original");

  const std::string replacement = File("atomic.next.tsv");
  Write(replacement, "100\tUpdated\n101\tNew\n");
  ASSERT_TRUE(FileUtil::AtomicRename(replacement, path).ok());
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("100"),
            std::vector<std::string>{"Updated"});
  EXPECT_EQ(manager.LookupExact("101"), std::vector<std::string>{"New"});
  EXPECT_EQ(old_snapshot->entries.at("100").at(0), "Original");
  EXPECT_EQ(manager.GetStatus().revision, 2);
}

TEST_F(ExternalDictionaryManagerTest, MalformedUpdateRetainsLastGoodSnapshot) {
  const std::string path = File("malformed.tsv");
  Write(path, "100\tCorrect\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  const auto before = manager.GetSnapshot();

  Write(path, "100\tCorrect\nBROKEN-ROW\n");
  EXPECT_FALSE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.GetSnapshot(), before);
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kError);
  EXPECT_TRUE(manager.GetStatus().serving_stale_data);
  EXPECT_EQ(manager.GetStatus().revision, 1);
  EXPECT_EQ(manager.LookupExact("100"),
            std::vector<std::string>{"Correct"});

  Write(path, "100\tRecovered\n");
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("100"),
            std::vector<std::string>{"Recovered"});
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kReady);
  EXPECT_FALSE(manager.GetStatus().serving_stale_data);
}

TEST_F(ExternalDictionaryManagerTest, DeletionDoesNotMasqueradeAsEmptyFile) {
  const std::string path = File("deleted.tsv");
  Write(path, "100\tOld\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  ASSERT_TRUE(FileUtil::Unlink(path).ok());
  EXPECT_FALSE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("100"), std::vector<std::string>{"Old"});

  Write(path, "100\tBack\n");
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("100"), std::vector<std::string>{"Back"});
}

TEST_F(ExternalDictionaryManagerTest, RejectsInvalidUtf8AndExtraColumns) {
  const std::string path = File("bad.tsv");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);

  Write(path, "k\tv\textra\n");
  EXPECT_FALSE(manager.PollOnceForTesting().ok());
  EXPECT_TRUE(manager.LookupExact("k").empty());

  Write(path, std::string("k\t") + "\xff" + "\n");
  EXPECT_FALSE(manager.PollOnceForTesting().ok());
  EXPECT_TRUE(manager.LookupExact("k").empty());

  Write(path, "k\tgood\n");
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("k"), std::vector<std::string>{"good"});
}

TEST_F(ExternalDictionaryManagerTest, SameBytesDoNotAdvanceRevision) {
  const std::string path = File("stable.tsv");
  Write(path, "42\tValue\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  const auto snapshot = manager.GetSnapshot();
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.GetStatus().revision, 1);
  EXPECT_EQ(manager.GetSnapshot(), snapshot);
}

TEST_F(ExternalDictionaryManagerTest, EmptyFileIsValidAndClearsPreviousData) {
  const std::string path = File("empty.tsv");
  Write(path, "42\tValue\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  Write(path, "");
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_TRUE(manager.LookupExact("42").empty());
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kReady);
  EXPECT_EQ(manager.GetStatus().entry_count, 0);
}

TEST_F(ExternalDictionaryManagerTest, RejectsOversizedFile) {
  const std::string path = File("large.tsv");
  Write(path, std::string(16 * 1024 * 1024 + 1, 'x'));
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  EXPECT_FALSE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kError);
}

TEST_F(ExternalDictionaryManagerTest, WorkerPicksUpChangesWithoutRestart) {
  const std::string path = File("hot.tsv");
  Write(path, "777\tBefore\n");
  ExternalDictionaryManager manager(std::chrono::milliseconds(20));
  manager.SetPath(path);

  const auto await_value = [&](const std::string& expected) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      if (manager.LookupExact("777") ==
          std::vector<std::string>{expected}) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  };

  ASSERT_TRUE(await_value("Before"));
  const std::string next = File("hot.next.tsv");
  Write(next, "777\tAfter\n");
  ASSERT_TRUE(FileUtil::AtomicRename(next, path).ok());
  EXPECT_TRUE(await_value("After"));
  manager.Stop();
}

TEST_F(ExternalDictionaryManagerTest, LookupIsSafeDuringRepeatedSwaps) {
  const std::string path = File("concurrent.tsv");
  Write(path, "k\tA\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPath(path);
  ASSERT_TRUE(manager.PollOnceForTesting().ok());

  std::atomic<bool> done(false);
  std::atomic<bool> invalid(false);
  std::thread reader([&] {
    while (!done.load()) {
      const auto values = manager.LookupExact("k");
      if (values.size() != 1 ||
          (values[0] != "A" && values[0] != "B")) {
        invalid.store(true);
      }
    }
  });
  for (int i = 0; i < 80; ++i) {
    Write(path, i % 2 == 0 ? "k\tB\n" : "k\tA\n");
    EXPECT_TRUE(manager.PollOnceForTesting().ok());
  }
  done.store(true);
  reader.join();
  EXPECT_FALSE(invalid.load());
}

TEST_F(ExternalDictionaryManagerTest, MultipleFilesMergeInUserSpecifiedOrder) {
  const std::string a = File("a.tsv");
  const std::string b = File("b.tsv");
  Write(a, "ぐう\t第一ファイル\nおなじ\t重複\n");
  Write(b, "ぐう\t第二ファイル\nおなじ\t重複\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPaths({a, b, a});
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("ぐう"),
            (std::vector<std::string>{"第一ファイル", "第二ファイル"}));
  EXPECT_EQ(manager.LookupExact("おなじ"),
            (std::vector<std::string>{"重複"}));
  EXPECT_EQ(manager.GetStatus().entry_count, 3);

  manager.SetPaths({b, a});
  EXPECT_EQ(manager.LookupExact("ぐう"),
            (std::vector<std::string>{"第二ファイル", "第一ファイル"}));
  manager.SetPaths({b});
  EXPECT_EQ(manager.LookupExact("ぐう"),
            (std::vector<std::string>{"第二ファイル"}));
  manager.SetPaths({});
  EXPECT_TRUE(manager.LookupExact("ぐう").empty());
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kDisabled);
}

TEST_F(ExternalDictionaryManagerTest, FailureInOneFilePreservesOthers) {
  const std::string a = File("keep.tsv");
  const std::string b = File("broken.tsv");
  Write(a, "あ\tA\n");
  Write(b, "い\tB\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPaths({a, b});
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  const auto old = manager.GetSnapshot();
  Write(a, "あ\tNewA\n");
  Write(b, "BROKEN\n");
  EXPECT_FALSE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("あ"), (std::vector<std::string>{"NewA"}));
  EXPECT_EQ(manager.LookupExact("い"), (std::vector<std::string>{"B"}));
  EXPECT_EQ(old->entries.at("あ").at(0), "A");
  EXPECT_TRUE(manager.GetStatus().serving_stale_data);
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kError);
  Write(b, "い\tNewB\n");
  ASSERT_TRUE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("い"), (std::vector<std::string>{"NewB"}));
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kReady);
}

TEST_F(ExternalDictionaryManagerTest, FailedNewFileDoesNotHideExistingFile) {
  const std::string a = File("working.tsv");
  const std::string b = File("missing.tsv");
  Write(a, "あ\tAvailable\n");
  ExternalDictionaryManager manager(std::chrono::seconds(3), false);
  manager.SetPaths({a, b});
  EXPECT_FALSE(manager.PollOnceForTesting().ok());
  EXPECT_EQ(manager.LookupExact("あ"),
            (std::vector<std::string>{"Available"}));
  manager.SetPaths({a});
  EXPECT_EQ(manager.GetStatus().state,
            ExternalDictionaryManager::State::kReady);
}

}  // namespace
}  // namespace mozc::dictionary
