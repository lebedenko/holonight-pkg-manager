#include "fixture_root.h"
#include "guard_snapshot.h"
#include "promotion.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>

namespace holonight_packages_transaction {
namespace {
namespace fs = std::filesystem;

class UsernsPromotionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(::unshare(CLONE_NEWNS), 0) << std::strerror(errno);
    ASSERT_EQ(::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr), 0) << std::strerror(errno);
    for (const auto& directory : {scratch(), live(), rollback()}) {
      fs::create_directories(directory);
    }
    ASSERT_EQ(::mount(scratch().c_str(), scratch().c_str(), nullptr, MS_BIND, nullptr), 0) << std::strerror(errno);
    mounted_ = true;
    for (const std::string name : {"core.db", "extra.db"}) {
      std::ofstream(scratch() / name) << "new " << name;
      std::ofstream(live() / name) << "old " << name;
    }
  }
  void TearDown() override {
    if (mounted_) {
      EXPECT_EQ(::umount(scratch().c_str()), 0) << std::strerror(errno);
    }
  }
  fs::path scratch() const { return world_.base() / "scratch"; }
  fs::path live() const { return world_.syncDbPath(); }
  fs::path rollback() const { return world_.base() / "rollback"; }
  LogSink journal() {
    return [this](LogLevel, const std::string& line) { lines_.push_back(line); };
  }
  holonight_transaction_testing::FixtureRoot world_;
  TransactionSeams seams_;
  std::vector<std::string> lines_;
  bool mounted_ = false;
};

TEST_F(UsernsPromotionTest, SameDeviceMountBoundaryReproducesRenameFailureAndPreservesLiveDatabases) {
  struct stat scratch_stat{};
  struct stat live_stat{};
  ASSERT_EQ(::stat(scratch().c_str(), &scratch_stat), 0);
  ASSERT_EQ(::stat(live().c_str(), &live_stat), 0);
  ASSERT_EQ(scratch_stat.st_dev, live_stat.st_dev);
  ASSERT_EQ(std::rename((scratch() / "core.db").c_str(), (live() / "core.db").c_str()), -1);
  const int rename_error = errno;
  ASSERT_EQ(rename_error, EXDEV);
  const auto core = sha256Of(live() / "core.db");
  const auto extra = sha256Of(live() / "extra.db");
  SyncPromotion promotion(scratch(), live(), rollback(), seams_, journal());
  EXPECT_FALSE(promotion.promote());
  EXPECT_EQ(sha256Of(live() / "core.db"), core);
  EXPECT_EQ(sha256Of(live() / "extra.db"), extra);
  EXPECT_THAT(lines_,
              ::testing::Contains(::testing::AllOf(
                  ::testing::HasSubstr("rename source=" + (scratch() / "core.db").string()),
                  ::testing::HasSubstr("destination=" + (live() / "core.db").string()),
                  ::testing::HasSubstr("error=" + std::to_string(EXDEV)), ::testing::HasSubstr("category=generic"))));
}

TEST_F(UsernsPromotionTest, ForcedCopyCrossesMountBoundaryAndPreservesContentsAndTimes) {
  seams_.promote_by_copy = true;
  const auto time = fs::file_time_type::clock::now() - std::chrono::hours(24);
  for (const auto& entry : fs::directory_iterator(scratch())) {
    fs::last_write_time(entry.path(), time);
  }
  SyncPromotion promotion(scratch(), live(), rollback(), seams_, journal());
  ASSERT_TRUE(promotion.promote());
  for (const auto& entry : fs::directory_iterator(scratch())) {
    const auto target = live() / entry.path().filename();
    EXPECT_EQ(sha256Of(target), sha256Of(entry.path()));
    EXPECT_EQ(fs::last_write_time(target), fs::last_write_time(entry.path()));
  }
  for (const auto& directory : {live(), rollback()}) {
    for (const auto& entry : fs::directory_iterator(directory)) {
      EXPECT_FALSE(entry.path().filename().string().starts_with(".hn-promote-"));
    }
  }
  EXPECT_TRUE(lines_.empty());
}
}  // namespace
}  // namespace holonight_packages_transaction
