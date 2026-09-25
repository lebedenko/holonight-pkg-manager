#include "sync_database_files.h"

#include <QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace holonight_packages_backends {
namespace {

namespace fs = std::filesystem;

class SyncDatabaseFilesTest : public ::testing::Test {
 protected:
  [[nodiscard]] fs::path root() const { return {directory_.path().toStdString()}; }

  static void touch(const fs::path& path) { std::ofstream(path).put('x'); }

 private:
  QTemporaryDir directory_;
};

TEST_F(SyncDatabaseFilesTest, MissingDatabasePathMeansNoDatabases) {
  const auto result = oldestSyncDatabaseTime(root() / "absent");

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->has_value());
}

TEST_F(SyncDatabaseFilesTest, MissingSyncDirectoryMeansNoDatabases) {
  const auto result = oldestSyncDatabaseTime(root());

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->has_value());
}

TEST_F(SyncDatabaseFilesTest, SyncPathThatIsAFileMeansNoDatabases) {
  touch(root() / "sync");

  const auto result = oldestSyncDatabaseTime(root());

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->has_value());
}

TEST_F(SyncDatabaseFilesTest, SyncDirectoryWithoutDbFilesMeansNoDatabases) {
  fs::create_directory(root() / "sync");
  touch(root() / "sync" / "core.db.sig");
  touch(root() / "sync" / "notes.txt");

  const auto result = oldestSyncDatabaseTime(root());

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->has_value());
}

TEST_F(SyncDatabaseFilesTest, ReturnsMtimeOfOldestDbFile) {
  fs::create_directory(root() / "sync");
  touch(root() / "sync" / "core.db");
  touch(root() / "sync" / "extra.db");
  const auto now = fs::file_time_type::clock::now();
  fs::last_write_time(root() / "sync" / "core.db", now - std::chrono::hours{48});
  fs::last_write_time(root() / "sync" / "extra.db", now - std::chrono::hours{1});

  const auto result = oldestSyncDatabaseTime(root());

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->has_value());
  EXPECT_EQ(**result, fs::last_write_time(root() / "sync" / "core.db"));
}

TEST_F(SyncDatabaseFilesTest, LocalDatabaseCheckFailsWithoutLocalDirectory) {
  const auto result = checkLocalDatabase(root());

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("local package database directory"), std::string::npos);
}

TEST_F(SyncDatabaseFilesTest, LocalDatabaseCheckFailsWithoutVersionFile) {
  fs::create_directory(root() / "local");

  const auto result = checkLocalDatabase(root());

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("ALPM_DB_VERSION"), std::string::npos);
}

TEST_F(SyncDatabaseFilesTest, LocalDatabaseCheckPassesWithVersionFile) {
  fs::create_directory(root() / "local");
  touch(root() / "local" / "ALPM_DB_VERSION");

  EXPECT_TRUE(checkLocalDatabase(root()).has_value());
}

}  // namespace
}  // namespace holonight_packages_backends
