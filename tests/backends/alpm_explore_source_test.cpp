#include "holonight_packages_backends/alpm_explore_source.h"

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
#include <utility>
#include <vector>

namespace holonight_packages_backends {
namespace {

namespace fs = std::filesystem;

using holonight_packages_domain::ExploreSnapshot;
using holonight_packages_domain::ExploreSourceErrorCode;
using holonight_packages_domain::SyncPackage;
using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

fs::path exploreFixture() { return fs::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "explore"; }

AlpmExploreSourceOptions optionsFor(const fs::path& database_path) {
  return AlpmExploreSourceOptions{.databaseRoot = database_path, .databasePath = database_path};
}

// "repository/name" of every package, sorted.
std::vector<std::string> identitiesOf(const ExploreSnapshot& snapshot) {
  std::vector<std::string> identities;
  identities.reserve(snapshot.packages.size());
  for (const SyncPackage& package : snapshot.packages) {
    identities.push_back(package.repository + "/" + package.name);
  }
  std::ranges::sort(identities);
  return identities;
}

const SyncPackage* find(const ExploreSnapshot& snapshot, const std::string& repository, const std::string& name) {
  const auto found = std::ranges::find_if(snapshot.packages, [&](const SyncPackage& candidate) {
    return candidate.repository == repository && candidate.name == name;
  });
  return found == snapshot.packages.end() ? nullptr : &*found;
}

// A writable copy of the explore fixture (local/ and sync/) in a temporary directory.
class TemporaryExploreDatabase {
 public:
  TemporaryExploreDatabase() {
    EXPECT_TRUE(directory_.isValid());
    root_ = fs::path(directory_.path().toStdString()) / "db";
    fs::create_directories(root_);
    fs::copy(exploreFixture() / "local", root_ / "local", fs::copy_options::recursive);
    fs::copy(exploreFixture() / "sync", root_ / "sync", fs::copy_options::recursive);
  }

  [[nodiscard]] const fs::path& root() const { return root_; }
  [[nodiscard]] fs::path syncDir() const { return root_ / "sync"; }

 private:
  QTemporaryDir directory_;
  fs::path root_;
};

struct FileState {
  QByteArray sha256;
  std::uintmax_t size = 0;
  fs::file_time_type modified;

  bool operator==(const FileState&) const = default;
};

std::map<fs::path, FileState> syncFileStates(const fs::path& sync_dir) {
  std::map<fs::path, FileState> states;
  for (const auto& entry : fs::directory_iterator(sync_dir)) {
    QFile file(QString::fromStdString(entry.path().string()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    states[entry.path()] = FileState{.sha256 = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256),
                                     .size = fs::file_size(entry.path()),
                                     .modified = fs::last_write_time(entry.path())};
  }
  return states;
}

TEST(AlpmExploreSource, ListsEveryPackageOfEverySyncDatabase) {
  const AlpmExploreSource source(optionsFor(exploreFixture()));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_TRUE(snapshot->databasesFound);
  // foreign-tool is local only and never listed; dup appears once per repository.
  EXPECT_THAT(identitiesOf(*snapshot),
              ElementsAre("core/dup", "core/nano", "core/vim", "core/vim-runtime", "extra/bare", "extra/dup",
                          "extra/gvim", "extra/neovim", "extra/vimb"));
}

TEST(AlpmExploreSource, MapsAllFields) {
  const AlpmExploreSource source(optionsFor(exploreFixture()));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  const SyncPackage* vim = find(*snapshot, "core", "vim");
  ASSERT_NE(vim, nullptr);
  EXPECT_EQ(vim->version, "9.1-1");
  EXPECT_EQ(vim->description, "Vi Improved, a highly configurable text editor");
  EXPECT_EQ(vim->url, "vim.example.org");
  EXPECT_THAT(vim->licenses, ElementsAre("custom:vim"));
  EXPECT_THAT(vim->dependencies, ElementsAre("vim-runtime=9.1-1", "glibc>=2.38"));
  EXPECT_THAT(vim->optionalDependencies, ElementsAre("python: Python language support", "ruby: Ruby language support"));
  EXPECT_EQ(vim->downloadSizeBytes, 1500000U);
  EXPECT_EQ(vim->installedSizeBytes, 3500000U);
  const SyncPackage* neovim = find(*snapshot, "extra", "neovim");
  ASSERT_NE(neovim, nullptr);
  EXPECT_THAT(neovim->licenses, ElementsAre("Apache-2.0", "custom:vim"));
}

TEST(AlpmExploreSource, PackageWithoutOptionalFieldsHasEmptyFields) {
  const AlpmExploreSource source(optionsFor(exploreFixture()));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  const SyncPackage* bare = find(*snapshot, "extra", "bare");
  ASSERT_NE(bare, nullptr);
  EXPECT_TRUE(bare->description.empty());
  EXPECT_TRUE(bare->url.empty());
  EXPECT_TRUE(bare->licenses.empty());
  EXPECT_TRUE(bare->dependencies.empty());
  EXPECT_TRUE(bare->optionalDependencies.empty());
  EXPECT_FALSE(bare->installedVersion.has_value());
}

TEST(AlpmExploreSource, InstalledVersionsComeFromTheLocalDatabase) {
  const AlpmExploreSource source(optionsFor(exploreFixture()));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_EQ(find(*snapshot, "core", "vim")->installedVersion, "9.0-1");
  EXPECT_EQ(find(*snapshot, "core", "vim-runtime")->installedVersion, "9.1-1");
  EXPECT_FALSE(find(*snapshot, "extra", "vimb")->installedVersion.has_value());
}

TEST(AlpmExploreSource, DuplicateNamesInDifferentRepositoriesAreBothListed) {
  const AlpmExploreSource source(optionsFor(exploreFixture()));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_EQ(find(*snapshot, "core", "dup")->version, "1.0-1");
  EXPECT_EQ(find(*snapshot, "extra", "dup")->version, "2.0-1");
}

TEST(AlpmExploreSource, MissingSyncDirectoryMeansNoDatabases) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const fs::path root(directory.path().toStdString());
  fs::copy(exploreFixture() / "local", root / "local", fs::copy_options::recursive);
  const AlpmExploreSource source(optionsFor(root));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_FALSE(snapshot->databasesFound);
  EXPECT_TRUE(snapshot->packages.empty());
}

TEST(AlpmExploreSource, EmptySyncDirectoryMeansNoDatabases) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const fs::path root(directory.path().toStdString());
  fs::copy(exploreFixture() / "local", root / "local", fs::copy_options::recursive);
  fs::create_directory(root / "sync");
  const AlpmExploreSource source(optionsFor(root));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_FALSE(snapshot->databasesFound);
}

TEST(AlpmExploreSource, MissingLocalDatabaseIsAnError) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const fs::path root(directory.path().toStdString());
  fs::copy(exploreFixture() / "sync", root / "sync", fs::copy_options::recursive);
  const AlpmExploreSource source(optionsFor(root));

  const auto snapshot = source.loadPackages();

  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error().code, ExploreSourceErrorCode::DatabaseOpenFailed);
  EXPECT_FALSE(fs::exists(root / "local"));
}

TEST(AlpmExploreSource, DataAsOfIsTheOldestDatabaseModificationTime) {
  const TemporaryExploreDatabase database;
  const auto now = fs::file_time_type::clock::now();
  fs::last_write_time(database.syncDir() / "core.db", now - std::chrono::hours{72});
  fs::last_write_time(database.syncDir() / "extra.db", now - std::chrono::hours{1});
  const AlpmExploreSource source(optionsFor(database.root()));

  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_EQ(snapshot->dataAsOf,
            std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(database.syncDir() / "core.db")));
}

TEST(AlpmExploreSource, ReloadPicksUpManuallySyncedDatabases) {
  const TemporaryExploreDatabase database;
  const AlpmExploreSource source(optionsFor(database.root()));
  const auto first = source.loadPackages();
  ASSERT_TRUE(first.has_value()) << first.error().message;
  ASSERT_NE(find(*first, "extra", "vimb"), nullptr);

  // Simulate an out-of-band sync: extra.db is replaced by a database without vimb, newer than before.
  fs::remove(database.syncDir() / "extra.db");
  fs::copy_file(database.syncDir() / "core.db", database.syncDir() / "extra.db");
  fs::last_write_time(database.syncDir() / "extra.db", fs::file_time_type::clock::now() + std::chrono::hours{1});
  const auto second = source.loadPackages();

  ASSERT_TRUE(second.has_value()) << second.error().message;
  EXPECT_EQ(find(*second, "extra", "vimb"), nullptr);
  EXPECT_NE(find(*second, "extra", "vim"), nullptr);
}

TEST(AlpmExploreSource, ReloadPicksUpLocalInstallChanges) {
  const TemporaryExploreDatabase database;
  const AlpmExploreSource source(optionsFor(database.root()));
  ASSERT_TRUE(source.loadPackages().has_value());

  fs::remove_all(database.root() / "local" / "vim-9.0-1");
  const auto snapshot = source.loadPackages();

  ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
  EXPECT_FALSE(find(*snapshot, "core", "vim")->installedVersion.has_value());
}

TEST(AlpmExploreSource, LoadingNeverModifiesSyncDatabases) {
  const TemporaryExploreDatabase database;
  const auto before = syncFileStates(database.syncDir());
  const AlpmExploreSource source(optionsFor(database.root()));

  ASSERT_TRUE(source.loadPackages().has_value());
  ASSERT_TRUE(source.loadPackages().has_value());

  EXPECT_EQ(syncFileStates(database.syncDir()), before);
}

}  // namespace
}  // namespace holonight_packages_backends
