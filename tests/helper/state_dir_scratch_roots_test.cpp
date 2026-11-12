#include "StateDirScratchRoots.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

namespace holonight_helper {
namespace {

namespace fs = std::filesystem;

class StateDirScratchRootsTest : public ::testing::Test {
 public:
  [[nodiscard]] fs::path base() const { return {directory_.path().toStdString()}; }
  [[nodiscard]] fs::path state() const { return base() / "state"; }

  QTemporaryDir directory_;
};

// system-upgrade T-042, REQ-F-008: owner and mode of the state directory and a scratch root.
TEST_F(StateDirScratchRootsTest, CreateGivesAPrivateTraversableDirectoryUnderTheStateDirectory) {
  StateDirScratchRoots roots(state());
  const auto root = roots.create();
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(root->parent_path(), state());
  EXPECT_TRUE(root->filename().string().starts_with("upgrade-"));
  for (const fs::path& path : {state(), *root}) {
    struct stat status{};
    ASSERT_EQ(::stat(path.c_str(), &status), 0);
    EXPECT_EQ(status.st_mode & 0777, 0711U) << path;
    EXPECT_EQ(status.st_uid, ::geteuid()) << path;  // root in production
  }
  EXPECT_NE(*roots.create(), *root);
}

TEST_F(StateDirScratchRootsTest, RemoveDeletesTheLocalSymlinkButNotWhatItPointsTo) {
  const fs::path live = base() / "live-local";
  fs::create_directories(live);
  std::ofstream(live / "desc") << "installed";
  StateDirScratchRoots roots(state());
  const auto root = roots.create();
  ASSERT_TRUE(root.has_value());
  fs::create_directories(*root / "dbpath");
  fs::create_directory_symlink(live, *root / "dbpath" / "local");
  roots.remove(*root);
  EXPECT_FALSE(fs::exists(*root));
  EXPECT_TRUE(fs::exists(live / "desc"));
}

TEST_F(StateDirScratchRootsTest, OnlyUpgradeEntriesOfTheStateDirectoryAreEverRemoved) {
  StateDirScratchRoots roots(state());
  const auto root = roots.create();
  ASSERT_TRUE(root.has_value());
  fs::create_directories(state() / "keep-me");
  std::ofstream(state() / "keep.txt") << "x";
  const fs::path elsewhere = base() / "upgrade-elsewhere";
  fs::create_directories(elsewhere);
  roots.remove(elsewhere);  // not in the state directory
  EXPECT_TRUE(fs::exists(elsewhere));
  roots.removeAll();
  EXPECT_FALSE(fs::exists(*root));
  EXPECT_TRUE(fs::exists(state() / "keep-me"));
  EXPECT_TRUE(fs::exists(state() / "keep.txt"));
}

}  // namespace
}  // namespace holonight_helper
