#include "holonight_packages_backends/alpm_package_source.h"

#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

namespace holonight_packages_backends {
namespace {

using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;
using holonight_packages_domain::SourceType;

std::filesystem::path fixturePath(const char* fixture_name) {
  return std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / fixture_name;
}

const Package* findByName(const std::vector<Package>& packages, const std::string& name) {
  const auto found = std::ranges::find_if(packages, [&name](const Package& package) { return package.name == name; });
  return found == packages.end() ? nullptr : &*found;
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

TEST(AlpmPackageSource, PopulatedFixtureReturnsThreePackagesWithMappedFields) {
  const auto root = fixturePath("populated");
  AlpmPackageSource source(root, root);

  const auto result = source.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), 3U);
}

TEST(AlpmPackageSource, OfficialPackagesAreMarkedOfficialWithRepository) {
  const auto root = fixturePath("populated");
  AlpmPackageSource source(root, root);

  const auto result = source.enumerateInstalledPackages();
  ASSERT_TRUE(result.has_value());

  const Package* apple = findByName(*result, "apple");
  ASSERT_NE(apple, nullptr);
  EXPECT_EQ(apple->sourceType, SourceType::Official);
  EXPECT_EQ(apple->repository, "core");
  EXPECT_EQ(apple->installedVersion, "2.3-4");
  EXPECT_EQ(apple->installReason, InstallReason::Explicit);

  const Package* zebra = findByName(*result, "zebra");
  ASSERT_NE(zebra, nullptr);
  EXPECT_EQ(zebra->sourceType, SourceType::Official);
  EXPECT_EQ(zebra->repository, "core");
  EXPECT_EQ(zebra->installReason, InstallReason::Dependency);
}

TEST(AlpmPackageSource, ForeignPackageIsMarkedForeignWithNoRepository) {
  const auto root = fixturePath("populated");
  AlpmPackageSource source(root, root);

  const auto result = source.enumerateInstalledPackages();
  ASSERT_TRUE(result.has_value());

  const Package* foreign_tool = findByName(*result, "foreign-tool");
  ASSERT_NE(foreign_tool, nullptr);
  EXPECT_EQ(foreign_tool->sourceType, SourceType::Foreign);
  EXPECT_TRUE(foreign_tool->repository.empty());
}

TEST(AlpmPackageSource, EmptyFixtureReturnsEmptyListWithoutError) {
  const auto root = fixturePath("empty");
  AlpmPackageSource source(root, root);

  const auto result = source.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

TEST(AlpmPackageSource, MissingSyncDirectoryMeansNoOfficialRepositories) {
  TemporaryDatabase database;
  std::filesystem::remove_all(database.root() / "sync");
  AlpmPackageSource source(database.root(), database.root());

  const auto result = source.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(
      std::ranges::all_of(*result, [](const Package& package) { return package.sourceType == SourceType::Foreign; }));
}

TEST(AlpmPackageSource, SyncPathThatIsNotADirectoryReturnsError) {
  TemporaryDatabase database;
  std::filesystem::remove_all(database.root() / "sync");
  std::ofstream(database.root() / "sync") << "not a directory";
  AlpmPackageSource source(database.root(), database.root());

  const auto result = source.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageSource, CorruptSyncDatabaseReturnsError) {
  TemporaryDatabase database;
  std::ofstream(database.root() / "sync" / "broken.db", std::ios::trunc) << "not an alpm database";
  AlpmPackageSource source(database.root(), database.root());

  const auto result = source.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageSource, OverlappingRepositoriesUseLexicallyFirstName) {
  TemporaryDatabase database;
  const auto sync_path = database.root() / "sync";
  std::filesystem::rename(sync_path / "core.db", sync_path / "zeta.db");
  std::filesystem::copy_file(sync_path / "zeta.db", sync_path / "alpha.db");
  AlpmPackageSource source(database.root(), database.root());

  const auto result = source.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  const Package* apple = findByName(*result, "apple");
  ASSERT_NE(apple, nullptr);
  EXPECT_EQ(apple->repository, "alpha");
}

TEST(AlpmPackageSource, InvalidRootPathReturnsErrorWithoutCrashing) {
  const std::filesystem::path invalid_root = fixturePath("populated") / "does-not-exist";
  AlpmPackageSource source(invalid_root, invalid_root);

  const auto result = source.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

}  // namespace
}  // namespace holonight_packages_backends
