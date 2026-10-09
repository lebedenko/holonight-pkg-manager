#include "file_repo_fixture.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace holonight_packages_testing {
namespace {

namespace fs = std::filesystem;

TEST(FileRepoFixture, BuildsMirrorAndRealDbpathUnderTheTempDirectory) {
  const FileRepoFixture fixture;
  EXPECT_TRUE(fs::exists(fixture.mirrorDir() / "core.db"));
  EXPECT_TRUE(fs::exists(fixture.mirrorDir() / "extra.db"));
  EXPECT_TRUE(fs::exists(fixture.realDbPath() / "local" / "ALPM_DB_VERSION"));
  EXPECT_TRUE(fs::exists(fixture.realDbPath() / "sync" / "core.db"));
  EXPECT_TRUE(fs::exists(fixture.confPath()));
  EXPECT_LT(fs::last_write_time(fixture.realDbPath() / "sync" / "core.db"),
            fs::last_write_time(fixture.mirrorDir() / "core.db"));
  EXPECT_NE(fs::file_size(fixture.realDbPath() / "sync" / "core.db"), fs::file_size(fixture.mirrorDir() / "core.db"));
}

TEST(FileRepoFixture, NothingNewMirrorServesTheOldDatabases) {
  const FileRepoFixture fixture(FileRepoFixture::Mirror::NothingNew);
  EXPECT_EQ(fs::file_size(fixture.realDbPath() / "sync" / "core.db"), fs::file_size(fixture.mirrorDir() / "core.db"));
}

TEST(FileRepoFixture, ConfServersAreFileUrls) {
  const FileRepoFixture fixture;
  std::ifstream input(fixture.confPath());
  std::stringstream text;
  text << input.rdbuf();
  EXPECT_NE(text.str().find("Server = file://" + fixture.mirrorDir().string()), std::string::npos);
}

using FileRepoFixtureDeathTest = ::testing::Test;

TEST(FileRepoFixtureDeathTest, WriteConfAbortsForANonFileUrl) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  const FileRepoFixture fixture;
  EXPECT_DEATH(fixture.writeConf({std::string("http") + "://mirror.invalid/$repo"}), "only file://");
}

TEST(FileRepoFixtureDeathTest, WriteConfAbortsForAFileUrlOutsideTheTempDirectory) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  const FileRepoFixture fixture;
  EXPECT_DEATH(fixture.writeConf({"file:///etc"}), "only file://");
  EXPECT_DEATH(fixture.writeConf({"file://" + fixture.root().string() + "/../escape"}), "only file://");
}

}  // namespace
}  // namespace holonight_packages_testing
