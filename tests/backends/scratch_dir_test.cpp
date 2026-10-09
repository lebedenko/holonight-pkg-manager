#include "scratch_dir.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace holonight_packages_backends {
namespace {

namespace fs = std::filesystem;

struct Entry {
  std::string sha;
  std::uintmax_t size = 0;
  fs::file_time_type modified;
  bool operator==(const Entry&) const = default;
};

std::map<fs::path, Entry> manifest(const fs::path& root) {
  std::map<fs::path, Entry> result;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    QFile file(QString::fromStdString(entry.path().string()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    result[entry.path()] = Entry{
        .sha = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toStdString(),
        .size = entry.file_size(),
        .modified = entry.last_write_time(),
    };
  }
  return result;
}

class ScratchDirTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    base_ = fs::path(dir_.path().toStdString());
    root_ = base_ / "cache" / "checkdb";
  }
  QTemporaryDir dir_;
  fs::path base_;
  fs::path root_;
};

TEST_F(ScratchDirTest, CreatedRootAndRunDirectoryAreOwnedByUsAndNotGroupOrWorldWritable) {
  ASSERT_TRUE(prepareScratchRoot(root_).has_value());
  const auto lock = ScratchLock::acquire(root_);
  ASSERT_TRUE(lock.has_value());
  const auto run = createRunDirectory(root_);
  ASSERT_TRUE(run.has_value());

  for (const fs::path& path : {root_, *run}) {
    struct stat info{};
    ASSERT_EQ(::lstat(path.c_str(), &info), 0);
    EXPECT_TRUE(S_ISDIR(info.st_mode));
    EXPECT_EQ(info.st_uid, ::geteuid());
    EXPECT_EQ(info.st_mode & 0022, 0U) << path;
    EXPECT_EQ(info.st_mode & 0777, 0700U) << path;
  }
  struct stat lockInfo{};
  ASSERT_EQ(::stat((root_ / "check.lock").c_str(), &lockInfo), 0);
  EXPECT_EQ(lockInfo.st_mode & 0777, 0600U);
}

TEST_F(ScratchDirTest, SymlinkedRootIsRejected) {
  fs::create_directories(base_ / "real");
  fs::create_directory_symlink(base_ / "real", base_ / "link");
  const auto result = prepareScratchRoot(base_ / "link");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ScratchFailure::Invalid);
}

TEST_F(ScratchDirTest, GroupWritableRootIsRejected) {
  fs::create_directories(root_);
  fs::permissions(root_, fs::perms::owner_all | fs::perms::group_write, fs::perm_options::replace);
  const auto result = prepareScratchRoot(root_);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ScratchFailure::Invalid);
}

TEST_F(ScratchDirTest, RootThatIsAFileIsRejected) {
  fs::create_directories(root_.parent_path());
  std::ofstream(root_) << "x";
  EXPECT_FALSE(prepareScratchRoot(root_).has_value());
}

TEST_F(ScratchDirTest, HeldLockReportsBusyWithoutStartingAnything) {
  ASSERT_TRUE(prepareScratchRoot(root_).has_value());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
  const int other = ::open((root_ / "check.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  ASSERT_GE(other, 0);
  ASSERT_EQ(::flock(other, LOCK_EX | LOCK_NB), 0);

  const auto lock = ScratchLock::acquire(root_);

  ASSERT_FALSE(lock.has_value());
  EXPECT_EQ(lock.error().kind, ScratchFailure::Busy);
  ::close(other);
  EXPECT_TRUE(ScratchLock::acquire(root_).has_value());
}

TEST_F(ScratchDirTest, LockFileThatIsASymlinkIsRefused) {
  ASSERT_TRUE(prepareScratchRoot(root_).has_value());
  std::ofstream(base_ / "elsewhere") << "x";
  fs::create_symlink(base_ / "elsewhere", root_ / "check.lock");
  const auto lock = ScratchLock::acquire(root_);
  ASSERT_FALSE(lock.has_value());
  EXPECT_EQ(lock.error().kind, ScratchFailure::Invalid);
}

// ---- T-021 ------------------------------------------------------------------------------------------------------

fs::path plantRun(const fs::path& root, const std::string& name, std::chrono::seconds age) {
  const fs::path run = root / name;
  fs::create_directories(run / "sync");
  std::ofstream(run / "sync" / "core.db") << name;
  fs::last_write_time(run, fs::file_time_type::clock::now() - age);
  return run;
}

TEST_F(ScratchDirTest, SweepKeepsOnlyTheNewestRunDirectoryAndTheLock) {
  ASSERT_TRUE(prepareScratchRoot(root_).has_value());
  const auto lock = ScratchLock::acquire(root_);
  ASSERT_TRUE(lock.has_value());
  plantRun(root_, "run-old111", std::chrono::hours{72});
  const fs::path newest = plantRun(root_, "run-new222", std::chrono::hours{1});
  plantRun(root_, "run-mid333", std::chrono::hours{24});
  std::ofstream(root_ / "stray.txt") << "x";
  fs::create_symlink("/nonexistent", root_ / "stray-link");

  sweepScratchRoot(root_);

  std::vector<std::string> remaining;
  for (const auto& entry : fs::directory_iterator(root_)) {
    remaining.push_back(entry.path().filename().string());
  }
  std::ranges::sort(remaining);
  EXPECT_EQ(remaining, (std::vector<std::string>{"check.lock", "run-new222"}));
  EXPECT_TRUE(fs::exists(newest / "sync" / "core.db"));

  const auto created = createRunDirectory(root_);
  ASSERT_TRUE(created.has_value());
  removeOtherRunDirectories(root_, *created);
  std::vector<fs::path> runs;
  for (const auto& entry : fs::directory_iterator(root_)) {
    if (entry.path().filename().string().starts_with("run-")) {
      runs.push_back(entry.path());
    }
  }
  EXPECT_EQ(runs, (std::vector<fs::path>{*created}));
}

TEST_F(ScratchDirTest, RemovalNeverFollowsTheLocalSymlinkIntoTheRealDatabase) {
  ASSERT_TRUE(prepareScratchRoot(root_).has_value());
  const fs::path sentinel = base_ / "real-local";
  fs::create_directories(sentinel / "pkg-1.0-1");
  std::ofstream(sentinel / "ALPM_DB_VERSION") << "9\n";
  std::ofstream(sentinel / "pkg-1.0-1" / "desc") << "%NAME%\npkg\n";
  const auto before = manifest(sentinel);
  const fs::path run = plantRun(root_, "run-stale99", std::chrono::hours{5});
  fs::create_directory_symlink(sentinel, run / "local");

  removeTreeWithoutFollowingLinks(run);

  EXPECT_FALSE(fs::exists(run));
  EXPECT_EQ(manifest(sentinel), before);
  EXPECT_TRUE(fs::is_directory(sentinel / "pkg-1.0-1"));
}

TEST_F(ScratchDirTest, SweepAlsoNeverFollowsLocalSymlinks) {
  ASSERT_TRUE(prepareScratchRoot(root_).has_value());
  const fs::path sentinel = base_ / "real-local";
  fs::create_directories(sentinel);
  std::ofstream(sentinel / "ALPM_DB_VERSION") << "9\n";
  const auto before = manifest(sentinel);
  plantRun(root_, "run-newest", std::chrono::hours{1});
  const fs::path stale = plantRun(root_, "run-stale", std::chrono::hours{30});
  fs::create_directory_symlink(sentinel, stale / "local");
  fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours{30});

  sweepScratchRoot(root_);

  EXPECT_FALSE(fs::exists(stale));
  EXPECT_EQ(manifest(sentinel), before);
}

TEST_F(ScratchDirTest, RemovingAMissingPathIsHarmless) {
  removeTreeWithoutFollowingLinks(base_ / "never-existed");
  SUCCEED();
}

}  // namespace
}  // namespace holonight_packages_backends
