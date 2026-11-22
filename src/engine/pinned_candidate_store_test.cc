// Copyright 2026 Mozkey contributors.
#include "engine/pinned_candidate_store.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "testing/gunit.h"

namespace mozc::engine {
namespace {

TEST(PinnedCandidateStoreTest, ReplacementPersistenceAndConditionalUnpin) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  const std::string path = FileUtil::JoinPath(temp_dir->path(), "pins.tsv");
  {
    PinnedCandidateStore store(path);
    EXPECT_TRUE(store.Pin("こうしょう", "考証"));
    ASSERT_TRUE(store.Lookup("こうしょう").has_value());
    EXPECT_EQ(*store.Lookup("こうしょう"), "考証");
    EXPECT_TRUE(store.Pin("こうしょう", "交渉"));
    EXPECT_FALSE(store.Unpin("こうしょう", "考証"));
  }
  {
    PinnedCandidateStore store(path);
    ASSERT_TRUE(store.Lookup("こうしょう").has_value());
    EXPECT_EQ(*store.Lookup("こうしょう"), "交渉");
    EXPECT_TRUE(store.Unpin("こうしょう", "交渉"));
    EXPECT_FALSE(store.Lookup("こうしょう").has_value());
    EXPECT_FALSE(store.Pin("x\ny", "foo"));
  }
}


// Invalid or oversized stores must never be silently truncated or replaced.
// After the user repairs the bytes, the same store instance must retry.
void CheckInvalidFileIsPreserved(const std::string& contents) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  const std::string path = FileUtil::JoinPath(temp_dir->path(), "pins.tsv");
  ASSERT_TRUE(FileUtil::SetContents(path, contents).ok());

  PinnedCandidateStore store(path);
  EXPECT_FALSE(store.Lookup("こうしょう").has_value());
  EXPECT_FALSE(store.Pin("こうしょう", "考証"));
  EXPECT_FALSE(store.Unpin("こうしょう", "交渉"));
  auto after = FileUtil::GetContents(path);
  ASSERT_TRUE(after.ok());
  EXPECT_TRUE(*after == contents);

  // A failed load must not become a cached, supposedly successful load.
  ASSERT_TRUE(FileUtil::SetContents(path, "こうしょう\t交渉\n").ok());
  ASSERT_TRUE(store.Lookup("こうしょう").has_value());
  EXPECT_EQ(store.Lookup("こうしょう"), "交渉");
  EXPECT_TRUE(store.Pin("こうしょう", "考証"));
  EXPECT_EQ(store.Lookup("こうしょう"), "考証");
}

TEST(PinnedCandidateStoreTest, InvalidFormatsPreserveOriginalFile) {
  const std::vector<std::string> malformed = {
      "こうしょう 交渉\n",             // no tab
      "こうしょう\t交渉\textra\n", // multiple tabs
      "\t交渉\n",                  // empty reading
      "こうしょう\t\n",             // empty value
      "こうしょう\t交渉\n\n",      // blank row
      "こうしょう\t交渉\nこうしょう\t考証\n", // duplicate key
      "こうしょう\t交渉\r\n",       // invalid control byte
      "こうしょう\t交渉\ninvalid",   // valid prefix, invalid tail
      "こうしょう\t交渉\n\x01\tX\n",  // control byte
      "こうしょう\t" + std::string(4097, 'x') + "\n",
  };
  for (const auto& contents : malformed) {
    SCOPED_TRACE(contents.substr(0, 80));
    CheckInvalidFileIsPreserved(contents);
  }
}

TEST(PinnedCandidateStoreTest, TooManyEntriesPreserveOriginalFile) {
  std::string contents;
  for (int i = 0; i < 4097; ++i) {
    contents.append("key").append(std::to_string(i)).append("\tvalue\n");
  }
  CheckInvalidFileIsPreserved(contents);
}

TEST(PinnedCandidateStoreTest, OversizedFilePreservesOriginalFile) {
  CheckInvalidFileIsPreserved(std::string(8 * 1024 * 1024 + 1, 'x'));
}

TEST(PinnedCandidateStoreTest, MaximumEntriesAllowsReplacementNotInsertion) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  const std::string path = FileUtil::JoinPath(temp_dir->path(), "pins.tsv");
  std::string contents;
  for (int i = 0; i < 4096; ++i) {
    contents.append("key").append(std::to_string(i)).append("\tvalue\n");
  }
  ASSERT_TRUE(FileUtil::SetContents(path, contents).ok());
  PinnedCandidateStore store(path);
  EXPECT_FALSE(store.Pin("extra", "value"));
  auto unchanged = FileUtil::GetContents(path);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_TRUE(*unchanged == contents);
  EXPECT_TRUE(store.Pin("key0", "replacement"));
  EXPECT_EQ(store.Lookup("key0"), "replacement");
}

TEST(PinnedCandidateStoreTest, MissingFileCanBeCreated) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  const std::string path = FileUtil::JoinPath(temp_dir->path(), "pins.tsv");
  PinnedCandidateStore store(path);
  EXPECT_FALSE(store.Lookup("こうしょう").has_value());
  EXPECT_TRUE(store.Pin("こうしょう", "考証"));
  auto contents = FileUtil::GetContents(path);
  ASSERT_TRUE(contents.ok());
  EXPECT_EQ(*contents, "こうしょう\t考証\n");
}

TEST(PinnedCandidateStoreTest, ReadFailureCannotCreateOrOverwrite) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  // DirectoryExists is successful, but attempting to read a directory as a
  // TSV file must not be interpreted as an empty store.
  const std::string path = temp_dir->path();
  PinnedCandidateStore store(path);
  EXPECT_FALSE(store.Lookup("こうしょう").has_value());
  EXPECT_FALSE(store.Pin("こうしょう", "考証"));
  EXPECT_FALSE(store.Unpin("こうしょう", "考証"));
  EXPECT_TRUE(FileUtil::DirectoryExists(path).ok());
  EXPECT_FALSE(FileUtil::FileExists(path + ".tmp").ok());
}

TEST(PinnedCandidateStoreTest, MutationRefreshesStaleCacheBeforeWrite) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  const std::string path = FileUtil::JoinPath(temp_dir->path(), "pins.tsv");
  PinnedCandidateStore store(path);
  ASSERT_TRUE(store.Pin("こうしょう", "考証"));
  ASSERT_TRUE(store.Lookup("こうしょう").has_value());

  // An external writer changes the file while this object is cached.
  const std::string external = "こうしょう\t交渉\nしよう\t仕様\n";
  ASSERT_TRUE(FileUtil::SetContents(path, external).ok());
  // A stale conditional unpin cannot erase a newer pin.
  EXPECT_FALSE(store.Unpin("こうしょう", "考証"));
  auto contents = FileUtil::GetContents(path);
  ASSERT_TRUE(contents.ok());
  EXPECT_EQ(*contents, external);
  EXPECT_TRUE(store.Pin("こうしょう", "公称"));
  contents = FileUtil::GetContents(path);
  ASSERT_TRUE(contents.ok());
  EXPECT_NE(contents->find("しよう\t仕様\n"), std::string::npos);
  EXPECT_NE(contents->find("こうしょう\t公称\n"), std::string::npos);
}

TEST(PinnedCandidateStoreTest, ConcurrentIndependentInstancesPreserveAllPins) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  const std::string path = FileUtil::JoinPath(temp_dir->path(), "pins.tsv");
  PinnedCandidateStore first(path);
  PinnedCandidateStore second(path);
  std::atomic<bool> start{false};
  std::atomic<int> failures{0};
  auto writer = [&](PinnedCandidateStore& store, const std::string& prefix) {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 32; ++i) {
      if (!store.Pin(prefix + std::to_string(i), "固定")) ++failures;
    }
  };
  std::thread a(writer, std::ref(first), "a");
  std::thread b(writer, std::ref(second), "b");
  start.store(true, std::memory_order_release);
  a.join();
  b.join();
  EXPECT_EQ(failures.load(), 0);
  PinnedCandidateStore after(path);
  for (int i = 0; i < 32; ++i) {
    const auto left = after.Lookup("a" + std::to_string(i));
    const auto right = after.Lookup("b" + std::to_string(i));
    ASSERT_TRUE(left.has_value());
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(*left, "固定");
    EXPECT_EQ(*right, "固定");
  }
  // The sidecar must persist; removing it would break POSIX lock identity.
  EXPECT_TRUE(FileUtil::FileExists(path + ".lock").ok());
}

TEST(PinnedCandidateStoreTest, CorruptionAfterSuccessfulLoadBlocksMutation) {
  auto temp_dir = mozc::TempDirectory::Default().CreateTempDirectory();
  ASSERT_TRUE(temp_dir.ok());
  const std::string path = FileUtil::JoinPath(temp_dir->path(), "pins.tsv");
  PinnedCandidateStore store(path);
  ASSERT_TRUE(store.Pin("こうしょう", "考証"));
  const std::string invalid = "こうしょう\t交渉\nmalformed\n";
  ASSERT_TRUE(FileUtil::SetContents(path, invalid).ok());
  EXPECT_FALSE(store.Pin("こうしょう", "公称"));
  EXPECT_FALSE(store.Unpin("こうしょう", "考証"));
  auto unchanged = FileUtil::GetContents(path);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_EQ(*unchanged, invalid);
}

}  // namespace
}  // namespace mozc::engine
