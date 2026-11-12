#include "HelperConfig.h"

#include <gtest/gtest.h>

namespace holonight_helper {
namespace {

// The first argument is the program name.
auto parse(QStringList arguments) {
  arguments.prepend(QStringLiteral("holonight-package-helper"));
  return parseHelperConfig(arguments);
}

TEST(HelperConfigTest, DefaultsAreTheSystemLocations) {
  const auto config = parse({});
  ASSERT_TRUE(config.has_value()) << config.error();
  EXPECT_EQ(config->transaction.root, "/");
  EXPECT_EQ(config->transaction.database_path, "/var/lib/pacman");
  EXPECT_EQ(config->transaction.pacman_conf_path, "/etc/pacman.conf");
  EXPECT_EQ(config->transaction.cache_directory, "/var/cache/pacman/pkg");
  EXPECT_EQ(config->state_directory, "/var/lib/holonight-package-helper");
}

TEST(HelperConfigTest, TheStateDirectoryCanBeMovedButMustBeAbsolute) {
  EXPECT_EQ(parse({"--state-dir=/tmp/s"})->state_directory, "/tmp/s");
  EXPECT_FALSE(parse({"--state-dir=relative"}).has_value());
}

TEST(HelperConfigTest, TheFourOptionsOverrideTheirPaths) {
  const auto config = parse({"--root=/tmp/r", "--dbpath=/tmp/d", "--pacman-conf=/tmp/c.conf", "--cache-dir=/tmp/k"});
  ASSERT_TRUE(config.has_value()) << config.error();
  EXPECT_EQ(config->transaction.root, "/tmp/r");
  EXPECT_EQ(config->transaction.database_path, "/tmp/d");
  EXPECT_EQ(config->transaction.pacman_conf_path, "/tmp/c.conf");
  EXPECT_EQ(config->transaction.cache_directory, "/tmp/k");
}

TEST(HelperConfigTest, RelativeEmptyAndUnknownArgumentsAreRejected) {
  EXPECT_FALSE(parse({"--root=relative/dir"}).has_value());
  EXPECT_FALSE(parse({"--dbpath="}).has_value());
  EXPECT_FALSE(parse({"--no-such-option=1"}).has_value());
  EXPECT_FALSE(parse({"stray"}).has_value());
  EXPECT_NE(parse({"--pacman-conf=conf"}).error().find("pacman-conf"), std::string::npos);
}

}  // namespace
}  // namespace holonight_helper
