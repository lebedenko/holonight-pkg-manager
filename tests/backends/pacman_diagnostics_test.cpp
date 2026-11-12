#include "pacman_repositories.h"
#include "pacman_settings.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_packages_backends {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class DiagnosticsTree {
 public:
  DiagnosticsTree() { EXPECT_TRUE(dir_.isValid()); }
  // Writes fixtures even when the path is unused.
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  std::filesystem::path write(const std::string& name, const std::string& contents) const {
    const auto path = std::filesystem::path(dir_.path().toStdString()) / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << contents;
    return path;
  }

 private:
  QTemporaryDir dir_;
};

// system-upgrade REQ-F-018: the upgrade names the repository that has no server.
TEST(PacmanDiagnostics, ARepositoryWithoutServerIsNamedAndTheResultIsUnchanged) {
  const DiagnosticsTree tree;
  const auto conf = tree.write("pacman.conf",
                               "[options]\n[core]\nServer = file:///core\n[extra]\nSigLevel = Never\n"
                               "[community]\n");
  PacmanDiagnostics diagnostics;
  const auto withDiagnostics = parsePacmanRepositories(conf, "x86_64", &diagnostics);
  const auto plain = parsePacmanRepositories(conf, "x86_64");
  ASSERT_FALSE(withDiagnostics.has_value());
  ASSERT_FALSE(plain.has_value());
  EXPECT_EQ(withDiagnostics.error(), plain.error());
  EXPECT_EQ(diagnostics.repository_without_server, "extra");
}

TEST(PacmanDiagnostics, AServerFromAnIncludedMirrorlistCounts) {
  const DiagnosticsTree tree;
  tree.write("mirrorlist", "Server = file:///mirror/$repo\n");
  const auto conf = tree.write("pacman.conf", "[options]\n[core]\nInclude = mirrorlist\n");
  PacmanDiagnostics diagnostics;
  ASSERT_TRUE(parsePacmanRepositories(conf, "x86_64", &diagnostics).has_value());
  EXPECT_EQ(diagnostics.repository_without_server, "");
  EXPECT_THAT(diagnostics.unsupported, IsEmpty());
}

// system-upgrade REQ-F-076 and DESIGN 5.2: the tokens the upgrade refuses, and the raw paths it compares.
TEST(PacmanDiagnostics, UnsupportedOptionsAreListedOnceInFileOrder) {
  const DiagnosticsTree tree;
  const auto conf = tree.write("pacman.conf",
                               "[options]\nXferCommand = /usr/bin/curl -o %o %u\nDBPath = /srv/db/\nRootDir = /srv\n"
                               "XferCommand = /usr/bin/wget %u\n[core]\nServer = file:///core\nUsage = Search\n"
                               "[extra]\nServer = file:///extra\nUsage = All\n");
  PacmanDiagnostics diagnostics;
  const auto parsed = parsePacmanRepositories(conf, "x86_64", &diagnostics);
  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  EXPECT_THAT(diagnostics.unsupported, ElementsAre("XferCommand", "Usage"));
  EXPECT_EQ(diagnostics.db_path, "/srv/db/");
  EXPECT_EQ(diagnostics.root_dir, "/srv");
  EXPECT_EQ(*parsed, *parsePacmanRepositories(conf, "x86_64"));
}

TEST(PacmanDiagnostics, UsageAllAloneIsSupported) {
  const DiagnosticsTree tree;
  const auto conf = tree.write("pacman.conf", "[options]\n[core]\nServer = file:///core\nUsage = All\n");
  PacmanDiagnostics diagnostics;
  ASSERT_TRUE(parsePacmanRepositories(conf, "x86_64", &diagnostics).has_value());
  EXPECT_THAT(diagnostics.unsupported, IsEmpty());
}

TEST(PacmanDiagnostics, TheSettingsLoaderPassesTheDiagnosticsThrough) {
  const DiagnosticsTree tree;
  const auto conf = tree.write("pacman.conf", "[options]\nXferCommand = /bin/true\n[core]\nServer = file:///core\n");
  PacmanDiagnostics diagnostics;
  const auto settings = loadPacmanSettings(conf, "x86_64", &diagnostics);
  ASSERT_TRUE(settings.has_value()) << settings.error();
  EXPECT_THAT(diagnostics.unsupported, ElementsAre("XferCommand"));
  ASSERT_TRUE(loadPacmanSettings(conf, "x86_64").has_value());
}

}  // namespace
}  // namespace holonight_packages_backends
