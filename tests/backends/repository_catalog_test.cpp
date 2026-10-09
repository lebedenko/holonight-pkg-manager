#include "UpdatesModel.h"
#include "file_repo_fixture.h"
#include "holonight_packages_application/update_monitor.h"
#include "holonight_packages_backends/alpm_update_checker.h"
#include "holonight_packages_backends/alpm_update_source.h"
#include "holonight_packages_persistence/catalog_lock.h"
#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include <QTest>

#include <fstream>
#include <future>
#include <gtest/gtest.h>

namespace holonight_packages_backends {
namespace {
namespace fs = std::filesystem;
using holonight_packages_testing::FileRepoFixture;

class RepositoryCatalogTest : public ::testing::Test {
 protected:
  FileRepoFixture fixture;
  fs::path snapshot = fixture.cacheHome() / "updates.json";
  AlpmUpdateChecker checker{{
      .databaseRoot = fixture.realDbPath(),
      .databasePath = fixture.realDbPath(),
      .pacmanConfPath = fixture.confPath(),
      .scratchRoot = fixture.scratchRoot(),
  }};
  holonight_packages_persistence::JsonUpdateSnapshotStore store{snapshot};

  std::shared_ptr<AlpmUpdateSource> source() {
    return std::make_shared<AlpmUpdateSource>(AlpmUpdateSourceOptions{
        .databaseRoot = fixture.realDbPath(),
        .databasePath = fixture.realDbPath(),
        .pacmanConfPath = fixture.confPath(),
        .snapshotFile = snapshot,
    });
  }
  void publish() {
    const auto result = checker.checkForUpdates();
    ASSERT_TRUE(result);
    ASSERT_TRUE(store.save({.snapshot = *result, .fetchedAt = std::chrono::system_clock::now()}));
  }
};

TEST_F(RepositoryCatalogTest, EqualOldestTimestampDoesNotDiscardCheckedExtra) {
  // An unchanged old core masks a newer extra when only the aggregate oldest timestamp is compared.
  fs::copy_file(fixture.realDbPath() / "sync/core.db", fixture.mirrorDir() / "core.db",
                fs::copy_options::overwrite_existing);
  fs::last_write_time(fixture.mirrorDir() / "core.db", fs::last_write_time(fixture.realDbPath() / "sync/core.db"));
  fs::copy_file(fs::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman/updates/extra-five.db",
                fixture.mirrorDir() / "extra.db", fs::copy_options::overwrite_existing);
  for (const auto* name : {"pending-a", "pending-b", "pending-c", "pending-d", "pending-e"}) {
    const auto installed = fixture.realDbPath() / "local" / (std::string(name) + "-1.0-1");
    fs::create_directory(installed);
    std::ofstream(installed / "desc") << "%NAME%\n" << name << "\n\n%VERSION%\n1.0-1\n\n%SIZE%\n100\n\n";
  }
  const auto local = source()->loadUpdates();
  ASSERT_TRUE(local);
  EXPECT_TRUE(local->updates.empty());
  publish();
  const auto saved = store.load();
  ASSERT_TRUE(saved && saved->snapshot);
  const auto result = source()->loadUpdates();
  ASSERT_TRUE(result);
  EXPECT_EQ(result->updates, saved->snapshot->snapshot.updates);
  EXPECT_EQ(result->updates.size(), 5U);
  auto backend = source();
  UpdatesModel rows(backend);
  QTRY_VERIFY_WITH_TIMEOUT(!rows.loading(), 3000);
  EXPECT_EQ(rows.updateCount(), 5);
  holonight_packages_application::UpdateMonitor monitor(backend, {});
  monitor.start();
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().updateCount, 5, 3000);
}

TEST_F(RepositoryCatalogTest, EqualTimestampDifferentContentsFavorsCheckedAndNewerLocalProducesMixedData) {
  publish();
  auto saved = store.load();
  ASSERT_TRUE(saved && saved->snapshot);
  for (const auto& repo : saved->snapshot->snapshot.repositories) {
    fs::last_write_time(fixture.realDbPath() / "sync" / (repo.name + ".db"),
                        std::chrono::clock_cast<fs::file_time_type::clock>(repo.timestamp));
  }
  const auto equal = source()->loadUpdates();
  ASSERT_TRUE(equal);
  EXPECT_EQ(equal->updates, saved->snapshot->snapshot.updates);
  fs::last_write_time(fixture.realDbPath() / "sync/core.db", fs::file_time_type::clock::now() + std::chrono::hours{1});
  const auto mixed = source()->loadUpdates();
  ASSERT_TRUE(mixed);
  ASSERT_EQ(mixed->repositories.size(), 2U);
  EXPECT_FALSE(mixed->repositories[0].checked);
  EXPECT_TRUE(mixed->repositories[1].checked);
}

TEST_F(RepositoryCatalogTest, InstalledRemovalRecalculatesWithoutCheckingOrChangingSnapshot) {
  publish();
  const auto before = store.load();
  fs::remove_all(fixture.realDbPath() / "local/alpha-1.0-1");
  const auto result = source()->loadUpdates();
  ASSERT_TRUE(result);
  EXPECT_EQ(std::ranges::count(result->updates, std::string("alpha"), &holonight_packages_domain::PendingUpdate::name),
            0);
  EXPECT_EQ(store.load()->snapshot, before->snapshot);
}

TEST_F(RepositoryCatalogTest, CorruptCatalogRemainsReadableAsLegacyRows) {
  publish();
  auto saved = store.load();
  ASSERT_TRUE(saved && saved->snapshot);
  std::ofstream(saved->snapshot->snapshot.repositories[0].database, std::ios::trunc) << "corrupt";
  auto damaged = store.load();
  ASSERT_TRUE(damaged && damaged->snapshot);
  EXPECT_TRUE(damaged->snapshot->snapshot.repositories.empty());
  EXPECT_EQ(damaged->snapshot->snapshot.updates, saved->snapshot->snapshot.updates);
  auto backend = source();
  const auto fallback = backend->loadUpdates();
  ASSERT_TRUE(fallback);
  EXPECT_TRUE(fallback->previously_loaded);
  EXPECT_EQ(fallback->updates, saved->snapshot->snapshot.updates);
  UpdatesModel rows(backend);
  holonight_packages_application::UpdateMonitor monitor(backend, {});
  monitor.adoptOnline(*saved->snapshot);
  monitor.start();
  QTRY_VERIFY_WITH_TIMEOUT(!rows.loading(), 3000);
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().updateCount, 3, 3000);
  EXPECT_EQ(rows.updateCount(), 3);
  EXPECT_EQ(rows.sourceText(), QStringLiteral("Previously loaded data"));
}

TEST_F(RepositoryCatalogTest, SignaturePolicyChangeRejectsCheckedEntries) {
  publish();
  fixture.writeConf({"file://" + fixture.mirrorDir().string()}, "Optional");
  const auto result = source()->loadUpdates();
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->updates.empty());
  for (const auto& repo : result->repositories) {
    EXPECT_FALSE(repo.checked);
  }
}

TEST_F(RepositoryCatalogTest, FailedPublicationRetainsPreviousCatalogAndSnapshot) {
  publish();
  const auto before = store.load();
  holonight_packages_persistence::JsonUpdateSnapshotStore failing(snapshot, {.beforeRename = [] { return false; }});
  const auto result = checker.checkForUpdates();
  ASSERT_TRUE(result);
  EXPECT_FALSE(
      failing.save({.snapshot = *result, .fetchedAt = std::chrono::system_clock::now() + std::chrono::hours{1}}));
  EXPECT_EQ(store.load()->snapshot, before->snapshot);
}

TEST_F(RepositoryCatalogTest, InstalledUpgradeAndAdditionRecalculateWithMirrorsAbsent) {
  publish();
  const auto before = store.load();
  fs::rename(fixture.mirrorDir(), fixture.root() / "offline-mirror");
  fs::remove_all(fixture.realDbPath() / "local/alpha-1.0-1");
  const auto upgraded = fixture.realDbPath() / "local/alpha-2.0-1";
  fs::create_directory(upgraded);
  std::ofstream(upgraded / "desc") << "%NAME%\nalpha\n\n%VERSION%\n2.0-1\n\n%SIZE%\n1500\n\n";
  fs::remove_all(fixture.realDbPath() / "local/gamma-1.0-1");
  const auto upgradedResult = source()->loadUpdates();
  ASSERT_TRUE(upgradedResult);
  EXPECT_EQ(std::ranges::count(upgradedResult->updates, std::string("alpha"),
                               &holonight_packages_domain::PendingUpdate::name),
            0);
  const auto added = fixture.realDbPath() / "local/gamma-1.0-1";
  fs::create_directory(added);
  std::ofstream(added / "desc") << "%NAME%\ngamma\n\n%VERSION%\n1.0-1\n\n%SIZE%\n5000\n\n";
  const auto addedResult = source()->loadUpdates();
  ASSERT_TRUE(addedResult);
  EXPECT_EQ(addedResult->updates.size(), upgradedResult->updates.size() + 1);
  EXPECT_EQ(store.load()->snapshot, before->snapshot);
}

TEST_F(RepositoryCatalogTest, CurrentRepositoryPriorityOverridesSavedOrder) {
  publish();
  const auto server = "file://" + fixture.mirrorDir().string();
  ASSERT_TRUE(fixture.isFixtureServer(server));
  std::ofstream(fixture.confPath()) << "[options]\nArchitecture = auto\nSigLevel = Never\n"
                                    << "[extra]\nServer = " << server << "\n[core]\nServer = " << server << "\n";
  const auto result = source()->loadUpdates();
  ASSERT_TRUE(result);
  const auto duplicate =
      std::ranges::find(result->updates, std::string("dup"), &holonight_packages_domain::PendingUpdate::name);
  ASSERT_NE(duplicate, result->updates.end());
  EXPECT_EQ(duplicate->repository, "extra");
  EXPECT_EQ(duplicate->availableVersion, "3.0-1");
}

TEST_F(RepositoryCatalogTest, PublicationWaitsForCatalogReadersBeforeReclamation) {
  publish();
  const auto result = checker.checkForUpdates();
  ASSERT_TRUE(result);
  auto lease = std::make_unique<holonight_packages_persistence::CatalogLock>(snapshot);
  ASSERT_TRUE(lease->valid());
  auto publication = std::async(std::launch::async, [&] {
    return store.save({.snapshot = *result, .fetchedAt = std::chrono::system_clock::now() + std::chrono::seconds{1}});
  });
  EXPECT_EQ(publication.wait_for(std::chrono::milliseconds{20}), std::future_status::timeout);
  lease.reset();
  ASSERT_TRUE(publication.get());
  const auto loaded = store.load();
  ASSERT_TRUE(loaded && loaded->snapshot);
  EXPECT_FALSE(loaded->snapshot->snapshot.repositories.empty());
}

TEST_F(RepositoryCatalogTest, FailedCheckDoesNotReplacePublishedCatalog) {
  publish();
  const auto before = store.load();
  fs::rename(fixture.mirrorDir(), fixture.root() / "missing-mirror");
  EXPECT_FALSE(checker.checkForUpdates());
  EXPECT_EQ(store.load()->snapshot, before->snapshot);
  const auto result = source()->loadUpdates();
  ASSERT_TRUE(result);
  EXPECT_EQ(result->updates, before->snapshot->snapshot.updates);
}

TEST_F(RepositoryCatalogTest, FilesystemRemovalUpdatesPageAndMonitorWithoutCompletingACheck) {
  publish();
  const auto before = store.load();
  auto backend = source();
  UpdatesModel rows(backend);
  holonight_packages_application::UpdateMonitor monitor(backend, {.debounce = std::chrono::milliseconds{10}});
  monitor.start();
  QTRY_VERIFY_WITH_TIMEOUT(!rows.loading(), 3000);
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().updateCount, 3, 3000);
  ASSERT_EQ(rows.updateCount(), 3);
  fs::remove_all(fixture.realDbPath() / "local/alpha-1.0-1");
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().updateCount, 2, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(rows.updateCount(), 2, 5000);
  EXPECT_EQ(store.load()->snapshot, before->snapshot);
  EXPECT_FALSE(store.loadHistory().has_value());
}

TEST_F(RepositoryCatalogTest, DeletedSnapshotAndDatabasesKeepPreviouslyLoadedRowsAndCount) {
  publish();
  auto backend = source();
  UpdatesModel rows(backend);
  holonight_packages_application::UpdateMonitor monitor(backend, {.debounce = std::chrono::milliseconds{10}});
  monitor.start();
  QTRY_VERIFY_WITH_TIMEOUT(!rows.loading(), 3000);
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().updateCount, 3, 3000);
  ASSERT_EQ(rows.updateCount(), 3);
  fs::remove(snapshot);
  fs::remove_all(fixture.realDbPath() / "sync");
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().state, holonight_packages_application::UpdateState::Error, 3000);
  QTRY_VERIFY_WITH_TIMEOUT(!rows.reloadErrorMessage().isEmpty(), 5000);
  EXPECT_EQ(monitor.status().updateCount, 3);
  EXPECT_EQ(rows.updateCount(), 3);
  EXPECT_EQ(rows.sourceText(), QStringLiteral("Previously loaded data"));
}

TEST_F(RepositoryCatalogTest, EqualNanosecondTimestampsSurvivePersistenceAndFavorCheckedContents) {
  auto checked = checker.checkForUpdates();
  ASSERT_TRUE(checked);
  for (auto& repository : checked->repositories) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(repository.timestamp);
    repository.timestamp = seconds + std::chrono::nanoseconds{123456789};
    fs::last_write_time(fixture.realDbPath() / "sync" / (repository.name + ".db"),
                        std::chrono::clock_cast<fs::file_time_type::clock>(repository.timestamp));
  }
  ASSERT_TRUE(store.save({.snapshot = *checked, .fetchedAt = std::chrono::system_clock::now()}));
  const auto loaded = store.load();
  ASSERT_TRUE(loaded && loaded->snapshot);
  ASSERT_EQ(loaded->snapshot->snapshot.repositories.size(), checked->repositories.size());
  EXPECT_EQ(loaded->snapshot->snapshot.repositories[0].timestamp, checked->repositories[0].timestamp);
  const auto result = source()->loadUpdates();
  ASSERT_TRUE(result);
  EXPECT_EQ(result->updates, checked->updates);
}

}  // namespace
}  // namespace holonight_packages_backends
