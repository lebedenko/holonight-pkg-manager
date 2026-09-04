#include "holonight_packages_persistence/alpm_connection_cache.h"

#include <QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>

namespace holonight_packages_persistence {
namespace {

std::filesystem::path fixturePath(const char* fixture_name) {
  return std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / fixture_name;
}

class TemporaryDatabase {
 public:
  TemporaryDatabase() {
    EXPECT_TRUE(directory_.isValid());
    root_ = std::filesystem::path(directory_.path().toStdString()) / "db";
    std::filesystem::copy(fixturePath("populated"), root_, std::filesystem::copy_options::recursive);
  }

  [[nodiscard]] const std::filesystem::path& root() const { return root_; }

 private:
  QTemporaryDir directory_;
  std::filesystem::path root_;
};

TEST(AlpmConnectionCache, FirstCallOnPopulatedFixtureRegistersSyncDatabases) {
  const auto root = fixturePath("populated");
  AlpmConnectionCache cache(root, root);

  const auto result = cache.connection();

  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->handle(), nullptr);
  EXPECT_EQ(result->syncDatabases().size(), 1U);
}

TEST(AlpmConnectionCache, UnchangedMtimeReusesParsedDatabase) {
  TemporaryDatabase database;
  AlpmConnectionCache cache(database.root(), database.root());
  const auto db_path = database.root() / "sync" / "core.db";
  const auto original_time = std::filesystem::last_write_time(db_path);

  {
    const auto warm = cache.connection();
    ASSERT_TRUE(warm.has_value());
  }
  std::ofstream(db_path, std::ios::trunc) << "not an alpm database";
  std::filesystem::last_write_time(db_path, original_time);

  const auto cached = cache.connection();

  ASSERT_TRUE(cached.has_value());
  EXPECT_EQ(cached->syncDatabases().size(), 1U);
}

TEST(AlpmConnectionCache, ChangedMtimeReparsesDatabase) {
  TemporaryDatabase database;
  AlpmConnectionCache cache(database.root(), database.root());

  {
    const auto warm = cache.connection();
    ASSERT_TRUE(warm.has_value());
  }

  const auto db_path = database.root() / "sync" / "core.db";
  std::ofstream(db_path, std::ios::trunc) << "not an alpm database";
  const auto advanced_time = std::filesystem::last_write_time(db_path) + std::chrono::seconds(5);
  std::filesystem::last_write_time(db_path, advanced_time);

  const auto reparsed = cache.connection();

  EXPECT_FALSE(reparsed.has_value());
}

TEST(AlpmConnectionCache, ConnectionLeaseSerializesConcurrentCalls) {
  TemporaryDatabase database;
  AlpmConnectionCache cache(database.root(), database.root());
  std::future<bool> second_call;

  {
    const auto first = cache.connection();
    ASSERT_TRUE(first.has_value());
    second_call = std::async(std::launch::async, [&cache] { return cache.connection().has_value(); });
    EXPECT_EQ(second_call.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  }

  ASSERT_EQ(second_call.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_TRUE(second_call.get());
}

TEST(AlpmConnectionCache, NewSyncDbFileIsRegisteredOnNextCall) {
  TemporaryDatabase database;
  AlpmConnectionCache cache(database.root(), database.root());

  {
    const auto result1 = cache.connection();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->syncDatabases().size(), 1U);
  }

  const auto sync_path = database.root() / "sync";
  std::filesystem::copy_file(sync_path / "core.db", sync_path / "extra.db");

  const auto result2 = cache.connection();

  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result2->syncDatabases().size(), 2U);
}

TEST(AlpmConnectionCache, RemovedSyncDbFileIsDroppedOnNextCall) {
  TemporaryDatabase database;
  AlpmConnectionCache cache(database.root(), database.root());

  {
    const auto result1 = cache.connection();
    ASSERT_TRUE(result1.has_value());
  }

  std::filesystem::remove(database.root() / "sync" / "core.db");

  const auto result2 = cache.connection();

  ASSERT_TRUE(result2.has_value());
  EXPECT_TRUE(result2->syncDatabases().empty());
}

TEST(AlpmConnectionCache, RegistrationFailureLeavesCacheRetryableAfterFixingTheFile) {
  TemporaryDatabase database;
  AlpmConnectionCache cache(database.root(), database.root());

  {
    const auto warm = cache.connection();
    ASSERT_TRUE(warm.has_value());
  }

  const auto db_path = database.root() / "sync" / "core.db";
  std::ofstream(db_path, std::ios::trunc) << "not an alpm database";

  const auto broken = cache.connection();
  ASSERT_FALSE(broken.has_value());

  std::filesystem::remove(db_path);

  const auto recovered = cache.connection();
  ASSERT_TRUE(recovered.has_value());
}

TEST(AlpmConnectionCache, MissingSyncDirectoryReturnsEmptyDatabasesWithoutError) {
  TemporaryDatabase database;
  std::filesystem::remove_all(database.root() / "sync");
  AlpmConnectionCache cache(database.root(), database.root());

  const auto result = cache.connection();

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->syncDatabases().empty());
}

TEST(AlpmConnectionCache, InvalidDatabaseRootReturnsError) {
  const auto invalid_root = fixturePath("populated") / "does-not-exist";
  AlpmConnectionCache cache(invalid_root, invalid_root);

  const auto result = cache.connection();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

}  // namespace
}  // namespace holonight_packages_persistence
