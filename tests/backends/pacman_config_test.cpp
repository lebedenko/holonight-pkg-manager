#include "pacman_config.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace holonight_packages_backends {
namespace {

using ::testing::ElementsAre;

class ConfigFile {
 public:
  explicit ConfigFile(const std::string& contents) {
    EXPECT_TRUE(directory_.isValid());
    path_ = std::filesystem::path(directory_.path().toStdString()) / "pacman.conf";
    std::ofstream(path_) << contents;
  }

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  QTemporaryDir directory_;
  std::filesystem::path path_;
};

TEST(PacmanConfig, AccumulatesIgnoreListsAcrossLines) {
  const ConfigFile file(
      "[options]\n"
      "IgnorePkg = linux linux-headers\n"
      "IgnorePkg=nvidia*\n"
      "IgnoreGroup = gnome\n"
      "  IgnoreGroup   =   kde-applications   xfce4  \n");

  const auto config = parsePacmanConfig(file.path());

  ASSERT_TRUE(config.has_value()) << config.error();
  EXPECT_THAT(config->ignorePkgs, ElementsAre("linux", "linux-headers", "nvidia*"));
  EXPECT_THAT(config->ignoreGroups, ElementsAre("gnome", "kde-applications", "xfce4"));
}

TEST(PacmanConfig, IgnoresCommentsUnknownKeysAndFlags) {
  const ConfigFile file(
      "# IgnorePkg = commented-out\n"
      "[options]\n"
      "#IgnorePkg = also-commented\n"
      "HoldPkg = pacman glibc\n"
      "Color\n"
      "IgnorePkg = kept   # trailing comment IgnorePkg = not-kept\n"
      "Architecture = auto\n");

  const auto config = parsePacmanConfig(file.path());

  ASSERT_TRUE(config.has_value()) << config.error();
  EXPECT_THAT(config->ignorePkgs, ElementsAre("kept"));
  EXPECT_TRUE(config->ignoreGroups.empty());
}

TEST(PacmanConfig, DoesNotCollectKeysOutsideOptions) {
  const ConfigFile file(
      "IgnorePkg = before-any-section\n"
      "[options]\n"
      "IgnorePkg = in-options\n"
      "[core]\n"
      "IgnorePkg = in-core\n"
      "IgnoreGroup = in-core-group\n"
      "Include = mirrorlist\n"
      "[options]\n"
      "IgnoreGroup = reopened-options\n");

  const auto config = parsePacmanConfig(file.path());

  ASSERT_TRUE(config.has_value()) << config.error();
  EXPECT_THAT(config->ignorePkgs, ElementsAre("in-options"));
  EXPECT_THAT(config->ignoreGroups, ElementsAre("reopened-options"));
}

TEST(PacmanConfig, EmptyFileGivesEmptyLists) {
  const ConfigFile file("");

  const auto config = parsePacmanConfig(file.path());

  ASSERT_TRUE(config.has_value()) << config.error();
  EXPECT_EQ(*config, PacmanConfig{});
}

TEST(PacmanConfig, FixtureConfigYieldsExpectedLists) {
  const auto config =
      parsePacmanConfig(std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "updates" / "pacman.conf");

  ASSERT_TRUE(config.has_value()) << config.error();
  EXPECT_THAT(config->ignorePkgs, ElementsAre("ign*", "never-installed"));
  EXPECT_THAT(config->ignoreGroups, ElementsAre("fruits"));
}

TEST(PacmanConfig, MissingFileIsAnError) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());

  const auto config = parsePacmanConfig(std::filesystem::path(directory.path().toStdString()) / "missing.conf");

  ASSERT_FALSE(config.has_value());
  EXPECT_NE(config.error().find("missing.conf"), std::string::npos);
}

TEST(PacmanConfig, DirectoryIsAnError) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());

  EXPECT_FALSE(parsePacmanConfig(directory.path().toStdString()).has_value());
}

TEST(PacmanConfig, UnreadableFileIsAnError) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "root ignores file permissions";
  }
  const ConfigFile file("[options]\nIgnorePkg = anything\n");
  std::filesystem::permissions(file.path(), std::filesystem::perms::none);

  EXPECT_FALSE(parsePacmanConfig(file.path()).has_value());
}

}  // namespace
}  // namespace holonight_packages_backends
