#include "pacman_settings.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_packages_backends {
namespace {

using ::testing::ElementsAre;

class Tree {
 public:
  Tree() { EXPECT_TRUE(dir_.isValid()); }
  [[nodiscard]] std::filesystem::path write(const std::string& name, const std::string& contents) const {
    const auto path = std::filesystem::path(dir_.path().toStdString()) / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << contents;
    return path;
  }
  [[nodiscard]] std::filesystem::path root() const { return dir_.path().toStdString(); }

 private:
  QTemporaryDir dir_;
};

TEST(PacmanSettings, LoadsOptionsAndRepositoriesFromOneFile) {
  const Tree tree;
  const auto conf = tree.write("pacman.conf",
                               "[options]\nHoldPkg = pacman\nIgnorePkg = foo\nSigLevel = Required\n"
                               "[core]\nServer = file:///fixture/$repo/$arch\n");
  const auto settings = loadPacmanSettings(conf, "x86_64");
  ASSERT_TRUE(settings) << settings.error();
  EXPECT_THAT(settings->options.hold_pkgs, ElementsAre("pacman"));
  EXPECT_THAT(settings->options.ignore_pkgs, ElementsAre("foo"));
  ASSERT_EQ(settings->repositories.repositories.size(), 1U);
  EXPECT_EQ(settings->repositories.repositories[0].servers[0], "file:///fixture/core/x86_64");
  EXPECT_TRUE(settings->repositories.repositories[0].sig_level_explicit);
}

TEST(PacmanSettings, IgnoresPathsAndExternalDownloadCommands) {
  const Tree tree;
  const auto conf = tree.write("pacman.conf",
                               "[options]\nDBPath = /etc/elsewhere/\nRootDir = /mnt/other\n"
                               "XferCommand = /usr/bin/curl %u\nGPGDir = /fixture/gnupg\n"
                               "[core]\nServer = file:///x\n");
  const auto settings = loadPacmanSettings(conf, "x86_64");
  ASSERT_TRUE(settings) << settings.error();
  // Nothing in PacmanSettings can carry a database path, a root or a download command.
  EXPECT_EQ(settings->options, PacmanConfig{});
  EXPECT_EQ(settings->repositories.gpg_directory, "/fixture/gnupg");
}

TEST(PacmanSettings, AnUnreadableFileAnUnparseableSigLevelAndAMissingServerAreErrors) {
  const Tree tree;
  EXPECT_FALSE(loadPacmanSettings(tree.root() / "absent.conf", "x86_64"));
  EXPECT_FALSE(loadPacmanSettings(tree.root(), "x86_64"));  // a directory
  EXPECT_FALSE(loadPacmanSettings(
      tree.write("bad.conf", "[options]\nSigLevel = Sometimes\n[core]\nServer = file:///x\n"), "x86_64"));
  EXPECT_FALSE(loadPacmanSettings(tree.write("noserver.conf", "[options]\n[core]\n"), "x86_64"));
}

}  // namespace
}  // namespace holonight_packages_backends
