#include "holonight_packages_backends/pacman_log_history_source.h"

#include "history_fixture.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using holonight_packages_backends::HistoryFileIo;
using holonight_packages_backends::PacmanLogHistorySource;
using holonight_packages_domain::HistoryEntry;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistoryReadMode;
using holonight_packages_domain::HistoryReadResult;
using holonight_packages_domain::HistoryStatus;
using holonight_test::HistoryFixture;
using holonight_test::HistoryIoRecord;
using ::testing::Each;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

std::string transaction(int index, bool completed = true) {
  const std::string name = "pkg" + std::to_string(index);
  std::string text = "[2026-11-23T10:00:00+0000] [PACMAN] Running 'pacman --sync " + name + "'\n";
  text += "[2026-11-23T10:00:01+0000] [ALPM] transaction started\n";
  text += "[2026-11-23T10:00:02+0000] [ALPM] installed " + name + " (1.0-1)\n";
  if (completed) {
    text += "[2026-11-23T10:00:03+0000] [ALPM] transaction completed\n";
    text += "[2026-11-23T10:00:04+0000] [ALPM] running '30-systemd-update.hook'...\n";
  }
  return text;
}

std::string transactions(int count) {
  std::string text;
  for (int i = 0; i < count; ++i) {
    text += transaction(i);
  }
  return text;
}

std::string fileContents(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::stringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

// Every entry under `root` with its type, size, modification time and contents (REQ-C-005).
std::string directorySnapshot(const std::filesystem::path& root) {
  std::vector<std::string> entries;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    std::string line = std::filesystem::relative(entry.path(), root).string();
    line += entry.is_directory() ? " dir " : " file ";
    line += std::to_string(entry.last_write_time().time_since_epoch().count());
    if (entry.is_regular_file()) {
      line += " " + std::to_string(entry.file_size()) + " " + fileContents(entry.path());
    }
    entries.push_back(std::move(line));
  }
  std::ranges::sort(entries);
  std::string snapshot;
  for (const std::string& entry : entries) {
    snapshot += entry + "\n";
  }
  return snapshot;
}

class PacmanLogHistorySourceTest : public ::testing::Test {
 protected:
  [[nodiscard]] HistoryFixture& fixture() { return fixture_; }
  [[nodiscard]] HistoryIoRecord& record() { return *record_; }
  [[nodiscard]] std::shared_ptr<HistoryIoRecord> recordPtr() const { return record_; }

  [[nodiscard]] std::unique_ptr<PacmanLogHistorySource> makeSource(std::size_t chunk_size = std::size_t{1} << 20U) {
    auto options = fixture_.options(record_);
    options.chunk_size = chunk_size;
    return std::make_unique<PacmanLogHistorySource>(std::move(options));
  }

 private:
  HistoryFixture fixture_;
  std::shared_ptr<HistoryIoRecord> record_ = std::make_shared<HistoryIoRecord>();
};

TEST_F(PacmanLogHistorySourceTest, GuardedIoRejectsTheSystemLog) {
  const HistoryFileIo file_io = holonight_test::guardedIo(recordPtr());
  EXPECT_NONFATAL_FAILURE(EXPECT_EQ(file_io.open("/var/log/pacman.log", O_RDONLY), -1), "system pacman log");
}

TEST_F(PacmanLogHistorySourceTest, LogPathFromConf) {
  fixture().append(transactions(2));
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  EXPECT_EQ(source->resolvedLogPath(), fixture().log());
  EXPECT_EQ(result.append.size(), 2U);
  EXPECT_THAT(record().opened_paths, ::testing::ElementsAre(fixture().log().string()));
}

TEST_F(PacmanLogHistorySourceTest, DefaultLogPath) {
  fixture().writeConf({}, {});
  std::vector<std::string> opened;
  HistoryFileIo file_io;
  file_io.open = [&opened](const char* path, int /*flags*/) {
    opened.emplace_back(path);
    errno = ENOENT;  // the default file is never opened for real
    return -1;
  };
  PacmanLogHistorySource source(
      {.pacman_conf_path = fixture().conf(), .io = file_io, .lock_probe = {}, .chunk_size = 1024});
  const HistoryReadResult result = source.read(HistoryReadMode::Full);
  EXPECT_EQ(source.resolvedLogPath(), "/var/log/pacman.log");
  EXPECT_THAT(opened, ::testing::ElementsAre("/var/log/pacman.log"));
  EXPECT_TRUE(result.reset);
}

TEST_F(PacmanLogHistorySourceTest, MissingLogHasReason) {
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  EXPECT_TRUE(result.reset);
  EXPECT_THAT(result.append, IsEmpty());
  EXPECT_THAT(result.reason, HasSubstr(fixture().log().string()));
  EXPECT_THAT(result.reason, HasSubstr("does not exist"));
}

TEST_F(PacmanLogHistorySourceTest, UnreadableLogHasReason) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "root ignores file permissions";
  }
  fixture().append(transactions(1));
  std::filesystem::permissions(fixture().log(), std::filesystem::perms::none);
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  EXPECT_TRUE(result.reset);
  EXPECT_THAT(result.append, IsEmpty());
  EXPECT_THAT(result.reason, HasSubstr("Cannot read the pacman log"));
}

TEST_F(PacmanLogHistorySourceTest, UnreadableConfHasReason) {
  std::filesystem::remove(fixture().conf());
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  EXPECT_TRUE(result.reset);
  EXPECT_THAT(result.append, IsEmpty());
  EXPECT_THAT(result.reason, HasSubstr(fixture().conf().string()));
  EXPECT_THAT(record().opened_paths, IsEmpty());
}

TEST_F(PacmanLogHistorySourceTest, LogWithoutTransactionsIsEmptyWithoutReason) {
  fixture().append("[2026-11-23T10:00:00+0000] [PACMAN] synchronizing package lists\n");
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  EXPECT_TRUE(result.reset);
  EXPECT_THAT(result.append, IsEmpty());
  EXPECT_THAT(result.reason, IsEmpty());
}

TEST_F(PacmanLogHistorySourceTest, OpensReadOnlyAndLeavesTheLogUnchanged) {
  fixture().append(transactions(3) + transaction(3, false));
  const std::string before = fileContents(fixture().log());
  const auto mtime = std::filesystem::last_write_time(fixture().log());
  const auto source = makeSource();
  (void)source->read(HistoryReadMode::Full);
  fixture().touchLock();
  (void)source->read(HistoryReadMode::Incremental);
  fixture().removeLock();
  (void)source->read(HistoryReadMode::Incremental);
  EXPECT_EQ(fileContents(fixture().log()), before);
  EXPECT_EQ(std::filesystem::last_write_time(fixture().log()), mtime);
  ASSERT_EQ(record().open_flags.size(), 3U);
  for (const int flags : record().open_flags) {
    EXPECT_EQ(flags & O_ACCMODE, O_RDONLY);
    EXPECT_EQ(flags & (O_CREAT | O_TRUNC | O_APPEND), 0);
  }
}

TEST_F(PacmanLogHistorySourceTest, NoFilesWritten) {
  fixture().append(transactions(3));
  const std::string before_full = directorySnapshot(fixture().root());
  const auto source = makeSource();
  ASSERT_EQ(source->read(HistoryReadMode::Full).append.size(), 3U);
  EXPECT_EQ(directorySnapshot(fixture().root()), before_full);

  fixture().append(transaction(3));
  const std::string before_refresh = directorySnapshot(fixture().root());
  ASSERT_EQ(source->read(HistoryReadMode::Incremental).append.size(), 1U);
  EXPECT_EQ(directorySnapshot(fixture().root()), before_refresh);
}

TEST_F(PacmanLogHistorySourceTest, IncrementalReadsOnlyAppended) {
  fixture().append(transactions(1000));
  const auto source = makeSource();
  const HistoryReadResult first = source->read(HistoryReadMode::Full);
  ASSERT_EQ(first.append.size(), 1000U);

  const std::string appended = transaction(1000);
  fixture().append(appended);
  record().reset();
  const HistoryReadResult second = source->read(HistoryReadMode::Incremental);

  EXPECT_EQ(record().bytes_read, appended.size());
  EXPECT_FALSE(second.reset);
  EXPECT_EQ(second.drop_last, 0U);
  ASSERT_EQ(second.append.size(), 1U);
  EXPECT_EQ(second.append.front().actions.front().name, "pkg1000");

  // Applying the delta gives the same list as a fresh full read.
  std::vector<HistoryEntry> list = first.append;
  list.insert(list.end(), second.append.begin(), second.append.end());
  const auto fresh = makeSource();
  EXPECT_EQ(list, fresh->read(HistoryReadMode::Full).append);
}

TEST_F(PacmanLogHistorySourceTest, NoNewBytesGivesAnEmptyDeltaWithoutReading) {
  fixture().append(transactions(2));
  const auto source = makeSource();
  (void)source->read(HistoryReadMode::Full);
  record().reset();
  const HistoryReadResult result = source->read(HistoryReadMode::Incremental);
  EXPECT_EQ(record().pread_calls, 0U);
  EXPECT_FALSE(result.reset);
  EXPECT_EQ(result.drop_last, 0U);
  EXPECT_THAT(result.append, IsEmpty());
}

TEST_F(PacmanLogHistorySourceTest, HooksAfterTheLastReadReplaceTheLastEntry) {
  fixture().append(transaction(0) + transaction(1, false));
  const auto source = makeSource();
  const HistoryReadResult first = source->read(HistoryReadMode::Full);
  ASSERT_EQ(first.append.size(), 2U);
  EXPECT_EQ(first.append.back().status, HistoryStatus::Interrupted);

  fixture().append(
      "[2026-11-23T10:00:03+0000] [ALPM] transaction completed\n"
      "[2026-11-23T10:00:04+0000] [ALPM] running 'post.hook'...\n");
  const HistoryReadResult second = source->read(HistoryReadMode::Incremental);
  EXPECT_EQ(second.drop_last, 1U);
  ASSERT_EQ(second.append.size(), 1U);
  EXPECT_EQ(second.append.front().status, HistoryStatus::Completed);
  EXPECT_THAT(second.append.front().hooks, ::testing::ElementsAre("post.hook"));
}

TEST_F(PacmanLogHistorySourceTest, PartialLineNotConsumed) {
  const std::string complete = transaction(0, false);
  fixture().append(complete + "[2026-11-23T10:05:18+0200] [ALPM] transaction comp");
  const auto source = makeSource();
  const HistoryReadResult first = source->read(HistoryReadMode::Full);
  EXPECT_EQ(source->consumedOffset(), complete.size());
  ASSERT_EQ(first.append.size(), 1U);
  EXPECT_EQ(first.append.front().status, HistoryStatus::Interrupted);

  fixture().append("leted\n");
  const HistoryReadResult second = source->read(HistoryReadMode::Incremental);
  EXPECT_EQ(second.drop_last, 1U);
  ASSERT_EQ(second.append.size(), 1U);
  EXPECT_EQ(second.append.front().status, HistoryStatus::Completed);
}

TEST_F(PacmanLogHistorySourceTest, SmallChunksGiveTheSameEntries) {
  fixture().append(transactions(20));
  const auto whole = makeSource()->read(HistoryReadMode::Full).append;
  for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{64}, std::size_t{4099}}) {
    EXPECT_EQ(makeSource(chunk)->read(HistoryReadMode::Full).append, whole) << "chunk " << chunk;
  }
  EXPECT_EQ(whole.size(), 20U);
}

TEST_F(PacmanLogHistorySourceTest, OverLongLineIsSkipped) {
  const std::string long_line = "[2026-11-23T10:00:00+0000] [ALPM] warning: " + std::string(std::size_t{1} << 21U, 'x');
  fixture().append(transaction(0, false) + long_line + "\n" +
                   "[2026-11-23T10:00:03+0000] [ALPM] transaction completed\n" + transaction(1));
  for (const std::size_t chunk : {std::size_t{4099}, std::size_t{1} << 20U}) {
    const auto entries = makeSource(chunk)->read(HistoryReadMode::Full).append;
    ASSERT_EQ(entries.size(), 2U) << "chunk " << chunk;
    EXPECT_EQ(entries.front().status, HistoryStatus::Completed);
    EXPECT_THAT(entries.front().messages, IsEmpty());
  }
}

TEST_F(PacmanLogHistorySourceTest, OverLongPartialLineStaysUnconsumedUntilNewline) {
  const std::string initial = transaction(0);
  const std::string oversized = "[2026-11-23T10:05:18+0200] [ALPM] warning: " + std::string(1U << 20U, 'x');
  fixture().append(initial + oversized);
  const auto incremental = makeSource(4099);
  const auto first = incremental->read(HistoryReadMode::Full);
  ASSERT_EQ(first.append.size(), 1U);
  EXPECT_EQ(incremental->consumedOffset(), initial.size());

  // This valid-looking suffix is still part of the malformed line.
  const std::string tail = "[2026-11-23T10:05:18+0200] [ALPM] warning: oversized suffix\n" + transaction(1);
  fixture().append(tail);
  const auto second = incremental->read(HistoryReadMode::Incremental);
  EXPECT_FALSE(second.reset);
  EXPECT_EQ(second.drop_last, 0U);
  ASSERT_EQ(second.append.size(), 1U);
  EXPECT_EQ(incremental->consumedOffset(), initial.size() + oversized.size() + tail.size());

  const std::vector combined{first.append.front(), second.append.front()};
  EXPECT_EQ(makeSource(4099)->read(HistoryReadMode::Full).append, combined);
  EXPECT_EQ(makeSource(1U << 22U)->read(HistoryReadMode::Full).append, combined);
  EXPECT_THAT(combined.front().messages, IsEmpty());
}

TEST_F(PacmanLogHistorySourceTest, TruncateTriggersFullReparse) {
  fixture().append(transactions(5));
  const auto source = makeSource();
  (void)source->read(HistoryReadMode::Full);
  const std::string smaller = transactions(2);
  fixture().truncateAndWrite(smaller);
  record().reset();
  const HistoryReadResult result = source->read(HistoryReadMode::Incremental);
  EXPECT_TRUE(result.reset);
  EXPECT_EQ(record().bytes_read, smaller.size());
  EXPECT_EQ(result.append, makeSource()->read(HistoryReadMode::Full).append);
}

TEST_F(PacmanLogHistorySourceTest, RenameOverChangesInodeTriggersFullReparse) {
  const std::string text = transactions(3);
  fixture().append(text);
  const auto source = makeSource();
  (void)source->read(HistoryReadMode::Full);
  std::string replacement = text;
  replacement.replace(replacement.find("installed pkg0"), 14, "installed new0");  // same size, new content
  fixture().renameOver(replacement);
  record().reset();
  const HistoryReadResult result = source->read(HistoryReadMode::Incremental);
  EXPECT_TRUE(result.reset);
  EXPECT_EQ(record().bytes_read, replacement.size());
  ASSERT_EQ(result.append.size(), 3U);
  EXPECT_EQ(result.append.front().actions.front().name, "new0");
}

TEST_F(PacmanLogHistorySourceTest, LogPathChangeTriggersFullReparse) {
  fixture().append(transactions(2));
  const auto source = makeSource();
  (void)source->read(HistoryReadMode::Full);
  const auto other = fixture().root() / "other.log";
  std::ofstream(other) << transactions(1);
  fixture().writeConf(other, {});
  const HistoryReadResult result = source->read(HistoryReadMode::Incremental);
  EXPECT_TRUE(result.reset);
  EXPECT_EQ(result.append.size(), 1U);
  EXPECT_EQ(source->resolvedLogPath(), other);
}

TEST_F(PacmanLogHistorySourceTest, InProgressWithLock) {
  fixture().append(transaction(0) + transaction(1, false));
  fixture().touchLock();
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  ASSERT_EQ(result.append.size(), 2U);
  EXPECT_EQ(result.append.front().status, HistoryStatus::Completed);
  EXPECT_EQ(result.append.back().status, HistoryStatus::InProgress);
}

TEST_F(PacmanLogHistorySourceTest, LockRemovedWithoutNewBytes) {
  fixture().append(transaction(0, false));
  fixture().touchLock();
  const auto source = makeSource();
  EXPECT_EQ(source->read(HistoryReadMode::Full).append.back().status, HistoryStatus::InProgress);

  fixture().removeLock();
  record().reset();
  const HistoryReadResult result = source->read(HistoryReadMode::Incremental);
  EXPECT_EQ(record().bytes_read, 0U);
  EXPECT_EQ(result.drop_last, 1U);
  ASSERT_EQ(result.append.size(), 1U);
  EXPECT_EQ(result.append.front().status, HistoryStatus::Interrupted);
}

TEST_F(PacmanLogHistorySourceTest, InterruptedAtEndNoLock) {
  fixture().append(transaction(0, false));
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  ASSERT_EQ(result.append.size(), 1U);
  EXPECT_EQ(result.append.front().status, HistoryStatus::Interrupted);
}

TEST_F(PacmanLogHistorySourceTest, InterruptedStartStart) {
  fixture().append(transaction(0, false) + transaction(1));
  fixture().touchLock();  // the lock only matters for the open tail
  const auto source = makeSource();
  const HistoryReadResult result = source->read(HistoryReadMode::Full);
  ASSERT_EQ(result.append.size(), 2U);
  EXPECT_EQ(result.append.front().status, HistoryStatus::Interrupted);
  EXPECT_EQ(result.append.back().status, HistoryStatus::Completed);
}

TEST_F(PacmanLogHistorySourceTest, LockProbeSeamDecidesTheOpenTail) {
  fixture().append(transaction(0, false));
  auto options = fixture().options(recordPtr());
  std::vector<std::filesystem::path> probed;
  options.lock_probe = [&probed](const std::filesystem::path& lock_file) {
    probed.push_back(lock_file);
    return true;
  };
  PacmanLogHistorySource source(std::move(options));
  EXPECT_EQ(source.read(HistoryReadMode::Full).append.front().status, HistoryStatus::InProgress);
  EXPECT_THAT(probed, ::testing::ElementsAre(fixture().lockFile()));
}

TEST_F(PacmanLogHistorySourceTest, OriginsSurviveIncrementalReads) {
  fixture().append("[2026-11-23T10:00:00+0000] [HOLONIGHT] Running 'install foo' (uid 1000)\n");
  const auto source = makeSource();
  EXPECT_THAT(source->read(HistoryReadMode::Full).append, IsEmpty());
  fixture().append("[2026-11-23T10:00:01+0000] [ALPM] transaction started\n");
  const HistoryReadResult result = source->read(HistoryReadMode::Incremental);
  ASSERT_EQ(result.append.size(), 1U);
  EXPECT_EQ(result.append.front().origin.kind, HistoryOriginKind::Packages);
  EXPECT_EQ(result.append.front().origin.uid, 1000U);
}

}  // namespace
