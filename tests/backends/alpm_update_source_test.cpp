#include "holonight_packages_backends/alpm_update_source.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

namespace holonight_packages_backends {
namespace {

using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceErrorCode;
using ::testing::ElementsAre;

std::filesystem::path updatesFixture() {
  return std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "updates";
}

AlpmUpdateSourceOptions optionsFor(const std::filesystem::path& database_path) {
  return AlpmUpdateSourceOptions{
      .databaseRoot = database_path, .databasePath = database_path, .pacmanConfPath = updatesFixture() / "pacman.conf"};
}

std::vector<std::string> namesOf(const UpdateSnapshot& snapshot) {
  std::vector<std::string> names;
  names.reserve(snapshot.updates.size());
  for (const PendingUpdate& update : snapshot.updates) {
    names.push_back(update.name);
  }
  std::ranges::sort(names);
  return names;
}

const PendingUpdate* findUpdate(const UpdateSnapshot& snapshot, const std::string& name) {
  const auto found = std::ranges::find(snapshot.updates, name, &PendingUpdate::name);
  return found == snapshot.updates.end() ? nullptr : &*found;
}

// A writable copy of the updates fixture (local/ and sync/) in a temporary directory.
class TemporaryUpdatesDatabase {
 public:
  TemporaryUpdatesDatabase() {
    EXPECT_TRUE(directory_.isValid());
    root_ = std::filesystem::path(directory_.path().toStdString()) / "db";
    std::filesystem::create_directories(root_);
    std::filesystem::copy(updatesFixture() / "local", root_ / "local", std::filesystem::copy_options::recursive);
    std::filesystem::copy(updatesFixture() / "sync", root_ / "sync", std::filesystem::copy_options::recursive);
  }

  [[nodiscard]] const std::filesystem::path& root() const { return root_; }
  [[nodiscard]] std::filesystem::path syncDir() const { return root_ / "sync"; }

 private:
  QTemporaryDir directory_;
  std::filesystem::path root_;
};

struct FileState {
  QByteArray sha256;
  std::uintmax_t size = 0;
  std::filesystem::file_time_type modified;

  bool operator==(const FileState&) const = default;
};

std::map<std::filesystem::path, FileState> syncFileStates(const std::filesystem::path& sync_dir) {
  std::map<std::filesystem::path, FileState> states;
  for (const auto& entry : std::filesystem::directory_iterator(sync_dir)) {
    QFile file(QString::fromStdString(entry.path().string()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    states[entry.path()] = FileState{.sha256 = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256),
                                     .size = std::filesystem::file_size(entry.path()),
                                     .modified = std::filesystem::last_write_time(entry.path())};
  }
  return states;
}

TEST(AlpmUpdateSource, ListsOnlyStrictlyNewerOfficialPackages) {
  const AlpmUpdateSource source(optionsFor(updatesFixture()));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_TRUE(snapshot->databasesFound);
  // beta (equal), delta (older in sync) and omega (foreign) are not listed.
  EXPECT_THAT(namesOf(*snapshot), ElementsAre("alpha", "banana", "dup", "gamma", "ignoreme"));
}

TEST(AlpmUpdateSource, RowCarriesVersionsRepositoryAndSizes) {
  const AlpmUpdateSource source(optionsFor(updatesFixture()));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  const PendingUpdate* alpha = findUpdate(*snapshot, "alpha");
  ASSERT_NE(alpha, nullptr);
  EXPECT_EQ(*alpha, (PendingUpdate{.name = "alpha",
                                   .installedVersion = "1.0-1",
                                   .availableVersion = "2.0-1",
                                   .repository = "core",
                                   .downloadSizeBytes = 2048,
                                   .installedSizeDeltaBytes = 500,
                                   .ignored = false}));
  const PendingUpdate* gamma = findUpdate(*snapshot, "gamma");
  ASSERT_NE(gamma, nullptr);
  EXPECT_EQ(gamma->repository, "extra");
  EXPECT_EQ(gamma->downloadSizeBytes, 4096U);
  EXPECT_EQ(gamma->installedSizeDeltaBytes, -2000);
}

TEST(AlpmUpdateSource, FirstRegisteredRepositoryWins) {
  const AlpmUpdateSource source(optionsFor(updatesFixture()));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  const PendingUpdate* dup = findUpdate(*snapshot, "dup");
  ASSERT_NE(dup, nullptr);
  EXPECT_EQ(dup->repository, "core");
  EXPECT_EQ(dup->availableVersion, "2.0-1");
}

TEST(AlpmUpdateSource, FlagsExactlyIgnorePkgAndIgnoreGroupMatches) {
  const AlpmUpdateSource source(optionsFor(updatesFixture()));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  std::vector<std::string> ignored;
  for (const PendingUpdate& update : snapshot->updates) {
    if (update.ignored) {
      ignored.push_back(update.name);
    }
  }
  std::ranges::sort(ignored);
  // ignoreme via IgnorePkg "ign*", banana via IgnoreGroup "fruits". alpha is only ignored in a [core] section.
  EXPECT_THAT(ignored, ElementsAre("banana", "ignoreme"));
}

TEST(AlpmUpdateSource, DataAsOfIsOldestSyncDatabaseMtime) {
  const TemporaryUpdatesDatabase database;
  const auto now = std::filesystem::file_time_type::clock::now();
  const auto older = now - std::chrono::hours(72);
  std::filesystem::last_write_time(database.syncDir() / "core.db", now - std::chrono::hours(1));
  std::filesystem::last_write_time(database.syncDir() / "extra.db", older);
  const AlpmUpdateSource source(optionsFor(database.root()));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_EQ(snapshot->dataAsOf, std::chrono::clock_cast<std::chrono::system_clock>(older));
}

TEST(AlpmUpdateSource, MissingSyncDirectoryMeansNoDatabases) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto root = std::filesystem::path(directory.path().toStdString()) / "db";
  std::filesystem::create_directories(root);
  std::filesystem::copy(updatesFixture() / "local", root / "local", std::filesystem::copy_options::recursive);
  const AlpmUpdateSource source(optionsFor(root));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_FALSE(snapshot->databasesFound);
  EXPECT_TRUE(snapshot->updates.empty());
}

TEST(AlpmUpdateSource, MissingDatabasePathMeansNoDatabases) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const AlpmUpdateSource source(optionsFor(std::filesystem::path(directory.path().toStdString()) / "absent"));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_FALSE(snapshot->databasesFound);
}

TEST(AlpmUpdateSource, SyncDirectoryWithoutDatabasesMeansNoDatabases) {
  const TemporaryUpdatesDatabase database;
  std::filesystem::remove(database.syncDir() / "core.db");
  std::filesystem::remove(database.syncDir() / "extra.db");
  const AlpmUpdateSource source(optionsFor(database.root()));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_FALSE(snapshot->databasesFound);
}

TEST(AlpmUpdateSource, DatabasesWithNothingNewerAreUpToDate) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto root = std::filesystem::path(directory.path().toStdString()) / "db";
  std::filesystem::create_directories(root);
  const auto fixtures = std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman";
  std::filesystem::copy(fixtures / "empty" / "local", root / "local", std::filesystem::copy_options::recursive);
  std::filesystem::copy(updatesFixture() / "sync", root / "sync", std::filesystem::copy_options::recursive);
  const AlpmUpdateSource source(optionsFor(root));

  const auto snapshot = source.loadUpdates();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_TRUE(snapshot->databasesFound);
  EXPECT_TRUE(snapshot->updates.empty());
}

TEST(AlpmUpdateSource, MissingPacmanConfIsConfigurationInvalid) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  auto options = optionsFor(updatesFixture());
  options.pacmanConfPath = std::filesystem::path(directory.path().toStdString()) / "pacman.conf";
  const AlpmUpdateSource source(options);

  const auto snapshot = source.loadUpdates();

  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error().code, UpdateSourceErrorCode::ConfigurationInvalid);
  EXPECT_FALSE(snapshot.error().message.empty());
}

TEST(AlpmUpdateSource, MissingLocalDatabaseFailsWithoutCreatingFiles) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto root = std::filesystem::path(directory.path().toStdString());
  std::filesystem::copy(updatesFixture() / "sync", root / "sync", std::filesystem::copy_options::recursive);
  const auto before = syncFileStates(root / "sync");
  const AlpmUpdateSource source(optionsFor(root));

  const auto snapshot = source.loadUpdates();

  EXPECT_FALSE(std::filesystem::exists(root / "local"));
  EXPECT_EQ(syncFileStates(root / "sync"), before);
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error().code, UpdateSourceErrorCode::DatabaseOpenFailed);
  EXPECT_FALSE(snapshot.error().message.empty());
}

TEST(AlpmUpdateSource, EmptyLocalDatabaseFailsWithoutCreatingVersionFile) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto root = std::filesystem::path(directory.path().toStdString());
  std::filesystem::copy(updatesFixture() / "sync", root / "sync", std::filesystem::copy_options::recursive);
  std::filesystem::create_directory(root / "local");
  const auto before = syncFileStates(root / "sync");
  const AlpmUpdateSource source(optionsFor(root));

  const auto snapshot = source.loadUpdates();

  EXPECT_TRUE(std::filesystem::is_empty(root / "local"));
  EXPECT_EQ(syncFileStates(root / "sync"), before);
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error().code, UpdateSourceErrorCode::DatabaseOpenFailed);
  EXPECT_FALSE(snapshot.error().message.empty());
}

TEST(AlpmUpdateSource, MissingLocalDatabaseOnReloadFailsWithoutRecreatingFiles) {
  const TemporaryUpdatesDatabase database;
  const AlpmUpdateSource source(optionsFor(database.root()));
  ASSERT_TRUE(source.loadUpdates().has_value());
  std::filesystem::rename(database.root() / "local", database.root() / "local.saved");

  const auto snapshot = source.loadUpdates();

  EXPECT_FALSE(std::filesystem::exists(database.root() / "local"));
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error().code, UpdateSourceErrorCode::DatabaseOpenFailed);
}

TEST(AlpmUpdateSource, CorruptSyncDatabaseIsDatabaseOpenFailed) {
  const TemporaryUpdatesDatabase database;
  QFile corrupt(QString::fromStdString((database.syncDir() / "core.db").string()));
  ASSERT_TRUE(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
  corrupt.write("not an alpm database");
  corrupt.close();
  const AlpmUpdateSource source(optionsFor(database.root()));

  const auto snapshot = source.loadUpdates();

  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error().code, UpdateSourceErrorCode::DatabaseOpenFailed);
}

TEST(AlpmUpdateSource, LoadingNeverModifiesSyncDatabases) {
  const TemporaryUpdatesDatabase database;
  const auto before = syncFileStates(database.syncDir());
  ASSERT_EQ(before.size(), 2U);
  const AlpmUpdateSource source(optionsFor(database.root()));

  ASSERT_TRUE(source.loadUpdates().has_value());
  ASSERT_TRUE(source.loadUpdates().has_value());

  EXPECT_EQ(syncFileStates(database.syncDir()), before) << "a sync database changed content, size or mtime";
}

TEST(AlpmUpdateSource, UnchangedSyncDatabasesGiveStableResult) {
  const TemporaryUpdatesDatabase database;
  const AlpmUpdateSource source(optionsFor(database.root()));

  const auto first = source.loadUpdates();
  const auto second = source.loadUpdates();

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, *second);
}

TEST(AlpmUpdateSource, ChangedSyncDatabaseIsReflectedOnNextLoad) {
  const TemporaryUpdatesDatabase database;
  const AlpmUpdateSource source(optionsFor(database.root()));
  const auto first = source.loadUpdates();
  ASSERT_TRUE(first.has_value());
  ASSERT_THAT(namesOf(*first), ElementsAre("alpha", "banana", "dup", "gamma", "ignoreme"));

  // Simulate a manual sync that changed extra.db: it now carries core's packages only.
  const auto extra = database.syncDir() / "extra.db";
  const auto previous_time = std::filesystem::last_write_time(extra);
  std::filesystem::copy_file(database.syncDir() / "core.db", extra, std::filesystem::copy_options::overwrite_existing);
  std::filesystem::last_write_time(extra, previous_time + std::chrono::seconds(5));

  const auto second = source.loadUpdates();

  ASSERT_TRUE(second.has_value()) << second.error().message;
  EXPECT_THAT(namesOf(*second), ElementsAre("alpha", "dup"));
}

}  // namespace
}  // namespace holonight_packages_backends
