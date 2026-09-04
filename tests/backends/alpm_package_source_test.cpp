#include "holonight_packages_backends/alpm_package_source.h"

#include "alpm_package_conversion.h"

#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>
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

class PermissionsGuard {
 public:
  explicit PermissionsGuard(std::filesystem::path path) : path_(std::move(path)) {
    std::error_code error;
    permissions_ = std::filesystem::status(path_, error).permissions();
    EXPECT_FALSE(error);
  }

  ~PermissionsGuard() {
    std::error_code error;
    std::filesystem::permissions(path_, permissions_, error);
  }

  PermissionsGuard(const PermissionsGuard&) = delete;
  PermissionsGuard& operator=(const PermissionsGuard&) = delete;
  PermissionsGuard(PermissionsGuard&&) = delete;
  PermissionsGuard& operator=(PermissionsGuard&&) = delete;

 private:
  std::filesystem::path path_;
  std::filesystem::perms permissions_{};
};

TEST(AlpmPackageConversion, AcceptsValidStrings) {
  const auto result =
      detail::convertPackageFields("apple", "2.3-4", "core", SourceType::Official, InstallReason::Explicit);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->name, "apple");
  EXPECT_EQ(result->installedVersion, "2.3-4");
  EXPECT_EQ(result->repository, "core");
}

TEST(AlpmPackageConversion, RejectsNullName) {
  const auto result =
      detail::convertPackageFields(nullptr, "2.3-4", "core", SourceType::Official, InstallReason::Explicit);

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageConversion, RejectsNullVersion) {
  const auto result =
      detail::convertPackageFields("apple", nullptr, "core", SourceType::Official, InstallReason::Explicit);

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageConversion, RejectsNullRepository) {
  const auto result =
      detail::convertPackageFields("apple", "2.3-4", nullptr, SourceType::Official, InstallReason::Explicit);

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

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

TEST(AlpmPackageSource, MissingLocalDbVersionReturnsErrorWithoutCrashing) {
  TemporaryDatabase database;
  std::filesystem::remove(database.root() / "local" / "ALPM_DB_VERSION");
  AlpmPackageSource source(database.root(), database.root());

  const auto result = source.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageSource, GarbledLocalDbVersionReturnsErrorWithoutCrashing) {
  TemporaryDatabase database;
  std::ofstream(database.root() / "local" / "ALPM_DB_VERSION", std::ios::trunc) << "not-a-version";
  AlpmPackageSource source(database.root(), database.root());

  const auto result = source.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageSource, LocalDescMissingVersionFieldDoesNotCrash) {
  TemporaryDatabase database;
  std::ofstream(database.root() / "local" / "apple-2.3-4" / "desc", std::ios::trunc)
      << "%NAME%\napple\n\n%BASE%\napple\n\n%REASON%\n0\n\n%VALIDATION%\nnone\n\n";
  AlpmPackageSource source(database.root(), database.root());

  const auto result = source.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  const Package* apple = findByName(*result, "apple");
  ASSERT_NE(apple, nullptr);
  EXPECT_EQ(apple->installedVersion, "2.3-4") << "libalpm falls back to the directory-name version "
                                                 "(pkgname-pkgver-pkgrel) when %VERSION% is absent from desc";
  EXPECT_NE(findByName(*result, "zebra"), nullptr);
}

TEST(AlpmPackageSource, LocalDatabaseWithNoReadPermissionReturnsErrorWithoutCrashing) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "permission enforcement is bypassed when running as root";
  }
  TemporaryDatabase database;
  const auto local_dir = database.root() / "local";
  const PermissionsGuard permissions_guard(local_dir);
  std::error_code permission_error;
  std::filesystem::permissions(local_dir, std::filesystem::perms::none, permission_error);
  ASSERT_FALSE(permission_error);

  AlpmPackageSource source(database.root(), database.root());
  const auto result = source.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageSource, SyncDirectoryWithNoReadPermissionReturnsErrorWithoutCrashing) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "permission enforcement is bypassed when running as root";
  }
  TemporaryDatabase database;
  const auto sync_dir = database.root() / "sync";
  const PermissionsGuard permissions_guard(sync_dir);
  std::error_code permission_error;
  std::filesystem::permissions(sync_dir, std::filesystem::perms::none, permission_error);
  ASSERT_FALSE(permission_error);

  AlpmPackageSource source(database.root(), database.root());
  const auto result = source.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AlpmPackageSource, SecondCallWithUnchangedDatabaseReturnsIdenticalPackages) {
  const auto root = fixturePath("populated");
  AlpmPackageSource source(root, root);

  const auto result1 = source.enumerateInstalledPackages();
  const auto result2 = source.enumerateInstalledPackages();

  ASSERT_TRUE(result1.has_value());
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(*result1, *result2);
}

TEST(AlpmPackageSource, RepositoryChangeAfterWarmCacheIsReflectedOnNextCall) {
  TemporaryDatabase database;
  AlpmPackageSource source(database.root(), database.root());

  const auto warm = source.enumerateInstalledPackages();
  ASSERT_TRUE(warm.has_value());

  const auto sync_path = database.root() / "sync";
  std::filesystem::rename(sync_path / "core.db", sync_path / "zeta.db");
  std::filesystem::copy_file(sync_path / "zeta.db", sync_path / "alpha.db");

  const auto result = source.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  const Package* apple = findByName(*result, "apple");
  ASSERT_NE(apple, nullptr);
  EXPECT_EQ(apple->repository, "alpha");
}

TEST(AlpmPackageSource, LocalPackageChangeAfterWarmCacheIsReflectedOnNextCall) {
  TemporaryDatabase database;
  AlpmPackageSource source(database.root(), database.root());

  const auto warm = source.enumerateInstalledPackages();
  ASSERT_TRUE(warm.has_value());
  const Package* original_apple = findByName(*warm, "apple");
  ASSERT_NE(original_apple, nullptr);
  EXPECT_EQ(original_apple->installReason, InstallReason::Explicit);

  std::ofstream(database.root() / "local" / "apple-2.3-4" / "desc", std::ios::trunc)
      << "%NAME%\napple\n\n%VERSION%\n2.3-4\n\n%BASE%\napple\n\n%REASON%\n1\n\n%VALIDATION%\nnone\n\n";

  const auto refreshed = source.enumerateInstalledPackages();

  ASSERT_TRUE(refreshed.has_value());
  const Package* refreshed_apple = findByName(*refreshed, "apple");
  ASSERT_NE(refreshed_apple, nullptr);
  EXPECT_EQ(refreshed_apple->installReason, InstallReason::Dependency);
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
