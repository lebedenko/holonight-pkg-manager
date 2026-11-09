#include "fixture_root.h"

#include "manifest.h"
#include "repo_builder.h"

#include <alpm.h>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_transaction_testing {
namespace {

namespace fs = std::filesystem;
using ::testing::HasSubstr;

TEST(FixtureGuardsTest, RejectsTheFilesystemRootAndRealPacmanLocations) {
  const fs::path temp = fs::temp_directory_path();
  EXPECT_TRUE(FixtureRoot::rejectRoot("/", temp));
  EXPECT_TRUE(FixtureRoot::rejectRoot("/var/lib/pacman", temp));
  EXPECT_TRUE(FixtureRoot::rejectRoot("/var/lib/pacman/local", temp));
  EXPECT_TRUE(FixtureRoot::rejectRoot("/etc/pacman.conf", temp));
}

TEST(FixtureGuardsTest, RejectsDirectoriesOutsideTheTemporaryDirectory) {
  const fs::path temp = fs::temp_directory_path();
  EXPECT_TRUE(FixtureRoot::rejectRoot("/usr", temp));
  EXPECT_TRUE(FixtureRoot::rejectRoot(temp / ".." / "elsewhere", temp));
  EXPECT_FALSE(FixtureRoot::rejectRoot(temp / "holonight-fixture-abc", temp));
}

TEST(FixtureGuardsTest, RejectsNonFileServersAndServersOutsideTheWorld) {
  const fs::path world = fs::temp_directory_path() / "holonight-fixture-abc";
  EXPECT_THAT(*FixtureRoot::rejectServer("https://mirror.example/core", world), HasSubstr("file://"));
  EXPECT_TRUE(FixtureRoot::rejectServer("http://127.0.0.1:8080/x", world));
  EXPECT_TRUE(FixtureRoot::rejectServer("ftp://mirror.example/x", world));
  EXPECT_TRUE(FixtureRoot::rejectServer("file:///etc", world));
  EXPECT_TRUE(FixtureRoot::rejectServer("file://" + world.string() + "/../elsewhere", world));
  EXPECT_FALSE(FixtureRoot::rejectServer("file://" + world.string() + "/repos/core", world));
}

TEST(FixtureRootTest, CreatesAWorldInsideTheTempDirAndRemovesItAgain) {
  fs::path base;
  {
    const FixtureRoot world;
    base = world.base();
    EXPECT_FALSE(FixtureRoot::rejectRoot(base, fs::temp_directory_path()));
    EXPECT_TRUE(fs::is_directory(world.installRoot()));
    EXPECT_TRUE(fs::is_directory(world.syncDbPath()));
    EXPECT_TRUE(fs::is_regular_file(world.localDbPath() / "ALPM_DB_VERSION"));
    EXPECT_EQ(world.config().root, world.installRoot());
    EXPECT_EQ(world.config().pacman_conf_path, world.confPath());
  }
  EXPECT_FALSE(fs::exists(base));
}

TEST(FixtureRootDeathTest, ARemoteServerInTheConfigurationAbortsTheTest) {
  const FixtureRoot world;
  const std::string remote = std::string("[core]\nServer = ") + "https" + "://mirror.example/core\n";
  EXPECT_DEATH(world.writeConfText(remote), "not a file://");
  EXPECT_DEATH(world.writeConfText("[core]\nServer = file:///etc\n"), "outside the fixture world");
}

TEST(FixtureRootTest, WritesTheConfigurationWithFixturePaths) {
  const FixtureRoot world;
  world.writeConf({"core"}, "Required DatabaseOptional");
  std::ifstream input(world.confPath());
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_THAT(text, HasSubstr("CacheDir = " + world.cacheDir().string()));
  EXPECT_THAT(text, HasSubstr("SigLevel = Required DatabaseOptional"));
  EXPECT_THAT(text, HasSubstr("Server = file://" + world.repoDir("core").string()));
}

TEST(RepoBuilderTest, ProducesDatabasesAndPackagesThatLibalpmReads) {
  const FixtureRoot world;
  RepoBuilder repo(world, "core");
  repo.add(
      PackageSpec{.name = "app", .version = "1.0-1", .depends = {"lib"}, .files = {{"usr/share/app/file", "hello"}}});
  repo.add(PackageSpec{.name = "lib", .version = "2.0-1", .files = {{"usr/share/lib/file", "lib"}}});
  repo.publish();
  world.installPackage(PackageSpec{.name = "old", .version = "0.5-1", .files = {{"usr/share/old/file", "old"}}},
                       InstallReason::Dependency);

  alpm_errno_t error{};
  alpm_handle_t* handle = alpm_initialize(world.installRoot().c_str(), world.dbPath().c_str(), &error);
  ASSERT_NE(handle, nullptr);
  alpm_db_t* sync = alpm_register_syncdb(handle, "core", 0);
  ASSERT_NE(sync, nullptr);
  alpm_pkg_t* app = alpm_db_get_pkg(sync, "app");
  ASSERT_NE(app, nullptr);
  EXPECT_STREQ(alpm_pkg_get_version(app), "1.0-1");
  alpm_pkg_t* lib = alpm_db_get_pkg(sync, "lib");
  ASSERT_NE(lib, nullptr);
  EXPECT_STREQ(alpm_pkg_get_version(lib), "2.0-1");
  alpm_pkg_t* old = alpm_db_get_pkg(alpm_get_localdb(handle), "old");
  ASSERT_NE(old, nullptr);
  EXPECT_EQ(alpm_pkg_get_reason(old), ALPM_PKG_REASON_DEPEND);
  alpm_release(handle);

  EXPECT_EQ(world.installedVersions().at("old"), "0.5-1");
  EXPECT_EQ(world.readFile("usr/share/old/file"), "old");
}

TEST(RepoBuilderTest, MutationsChangeOnlyThePackageFile) {
  const FixtureRoot world;
  RepoBuilder repo(world, "core");
  repo.add(PackageSpec{.name = "a", .files = {{"usr/a", "a"}}});
  repo.add(PackageSpec{.name = "b", .files = {{"usr/b", "b"}}});
  repo.add(PackageSpec{.name = "c", .files = {{"usr/c", "c"}}});
  repo.publish();
  const auto before = holonight_packages_testing::manifestOf(world.syncDbPath());

  const auto goodHash = holonight_packages_testing::manifestOf(repo.dir()).at("a-1.0-1-any.pkg.tar.zst").sha256;
  repo.corruptPackage("a");
  const auto afterCorruption = holonight_packages_testing::manifestOf(repo.dir());
  EXPECT_NE(afterCorruption.at("a-1.0-1-any.pkg.tar.zst").sha256, goodHash);
  EXPECT_EQ(afterCorruption.at("a-1.0-1-any.pkg.tar.zst").size, fs::file_size(repo.packagePath("a")));

  repo.deletePackageFile("b");
  EXPECT_FALSE(fs::exists(repo.packagePath("b")));
  repo.makeUnreadable("c");
  EXPECT_EQ(fs::status(repo.packagePath("c")).permissions(), fs::perms::none);

  EXPECT_EQ(holonight_packages_testing::manifestOf(world.syncDbPath()), before);  // the database is untouched
}

TEST(RepoBuilderTest, TheSignerRunsForEveryPackageFile) {
  const FixtureRoot world;
  RepoBuilder repo(world, "core");
  std::vector<std::string> signed_files;
  repo.setSigner([&signed_files](const fs::path& file) {
    signed_files.push_back(file.filename().string());
    std::ofstream(file.string() + ".sig") << "signature";
  });
  repo.add(PackageSpec{.name = "a", .files = {{"usr/a", "a"}}});
  EXPECT_EQ(signed_files, std::vector<std::string>{"a-1.0-1-any.pkg.tar.zst"});
  EXPECT_TRUE(fs::exists(repo.packagePath("a").string() + ".sig"));
  repo.deleteSignature("a");
  EXPECT_FALSE(fs::exists(repo.packagePath("a").string() + ".sig"));
}

}  // namespace
}  // namespace holonight_transaction_testing
