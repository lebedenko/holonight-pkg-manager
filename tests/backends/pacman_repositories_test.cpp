#include "pacman_repositories.h"

#include <QTemporaryDir>

#include <alpm.h>
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

TEST(PacmanRepositories, IncludesRetainContextAndAccumulateSignatureDirectives) {
  const Tree tree;
  tree.write("nested", "SigLevel = DatabaseRequired\n");
  tree.write("options", "SigLevel = Required\nSigLevel = PackageTrustAll\n[core]\nInclude = repo\n");
  tree.write("repo", "Server = file:///fixture\nInclude = nested\nSigLevel = DatabaseTrustedOnly\n");
  const auto parsed = parsePacmanRepositories(tree.write("pacman.conf", "[options]\nInclude = options\n"), "x86_64");
  ASSERT_TRUE(parsed) << parsed.error();
  ASSERT_EQ(parsed->repositories.size(), 1U);
  EXPECT_EQ(parsed->repositories[0].sig_level,
            kSigPackage | kSigDatabase | kSigPackageMarginalOk | kSigPackageUnknownOk);
  tree.write("nested", "SigLevel = InvalidToken\n");
  EXPECT_FALSE(parsePacmanRepositories(tree.write("bad.conf", "[core]\nInclude = repo\n"), "x86_64"));
}

TEST(PacmanRepositories, ParsesServersIncludesAndSubstitutesRepoAndArch) {
  const Tree tree;
  tree.write("mirrorlist",
             "# comment\n"
             "Server = file:///mnt/one/$repo/os/$arch\n"
             "Server = file:///mnt/two/$repo/os/$arch  # trailing\n");
  const auto conf = tree.write("pacman.conf",
                               "[options]\nArchitecture = auto\n"
                               "[core]\nInclude = mirrorlist\n"
                               "[extra]\nServer = file:///srv/$repo/$arch\nInclude = mirrorlist\n");

  const auto parsed = parsePacmanRepositories(conf, "x86_64");

  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  ASSERT_EQ(parsed->repositories.size(), 2U);
  EXPECT_EQ(parsed->repositories[0].name, "core");
  EXPECT_THAT(parsed->repositories[0].servers,
              ElementsAre("file:///mnt/one/core/os/x86_64", "file:///mnt/two/core/os/x86_64"));
  EXPECT_THAT(
      parsed->repositories[1].servers,
      ElementsAre("file:///srv/extra/x86_64", "file:///mnt/one/extra/os/x86_64", "file:///mnt/two/extra/os/x86_64"));
  EXPECT_THAT(parsed->architectures, ElementsAre("x86_64"));
}

TEST(PacmanRepositories, RepositoryLevelSigLevelOverridesTheGlobalOne) {
  const Tree tree;
  const auto conf = tree.write("pacman.conf",
                               "[options]\nSigLevel = Required DatabaseOptional\n"
                               "[core]\nServer = file:///a\n"
                               "[extra]\nSigLevel = Never\nServer = file:///b\n"
                               "[multilib]\nSigLevel = DatabaseRequired\nServer = file:///c\n");

  const auto parsed = parsePacmanRepositories(conf, "x86_64");

  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  EXPECT_EQ(parsed->default_sig_level, kSigPackage | kSigDatabase | kSigDatabaseOptional);
  EXPECT_EQ(parsed->repositories[0].sig_level, kSigPackage | kSigDatabase | kSigDatabaseOptional);
  EXPECT_EQ(parsed->repositories[1].sig_level, 0U);
  EXPECT_EQ(parsed->repositories[2].sig_level, kSigPackage | kSigDatabase);
}

TEST(PacmanRepositories, TrustAllAndOptionalSetTheMatchingBits) {
  const Tree tree;
  const auto conf = tree.write("pacman.conf", "[options]\nSigLevel = Optional TrustAll\n[core]\nServer = file:///a\n");
  const auto parsed = parsePacmanRepositories(conf, "x86_64");
  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  const std::uint32_t expected = kSigPackage | kSigPackageOptional | kSigPackageMarginalOk | kSigPackageUnknownOk |
                                 kSigDatabase | kSigDatabaseOptional | kSigDatabaseMarginalOk | kSigDatabaseUnknownOk;
  EXPECT_EQ(parsed->default_sig_level, expected);
}

TEST(PacmanRepositories, UnparseableSigLevelTokenIsAParseError) {
  const Tree tree;
  const auto global = tree.write("a.conf", "[options]\nSigLevel = Requierd\n[core]\nServer = file:///a\n");
  const auto repo = tree.write("b.conf", "[core]\nSigLevel = DatabaseMaybe\nServer = file:///a\n");
  EXPECT_FALSE(parsePacmanRepositories(global, "x86_64").has_value());
  EXPECT_FALSE(parsePacmanRepositories(repo, "x86_64").has_value());
}

TEST(PacmanRepositories, DefaultLevelWhenNoneIsSetEqualsTheValueRecordedInSpikeS3) {
  const Tree tree;
  const auto conf = tree.write("pacman.conf", "[core]\nServer = file:///a\n");
  const auto parsed = parsePacmanRepositories(conf, "x86_64");
  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  // SPIKES.md S-3: Required + TrustedOnly for packages and databases, i.e. database level ALPM_SIG_DATABASE.
  EXPECT_EQ(parsed->repositories[0].sig_level &
                (kSigDatabase | kSigDatabaseOptional | kSigDatabaseMarginalOk | kSigDatabaseUnknownOk),
            static_cast<std::uint32_t>(ALPM_SIG_DATABASE));
  EXPECT_EQ(parsed->default_sig_level, kPacmanDefaultSigLevel);
  EXPECT_EQ(kSigDatabase, static_cast<std::uint32_t>(ALPM_SIG_DATABASE));
  EXPECT_EQ(kSigDatabaseOptional, static_cast<std::uint32_t>(ALPM_SIG_DATABASE_OPTIONAL));
  EXPECT_EQ(kSigPackage, static_cast<std::uint32_t>(ALPM_SIG_PACKAGE));
}

TEST(PacmanRepositories, GpgDirAndFailClosedCases) {
  const Tree tree;
  const auto withGpg = tree.write("g.conf", "[options]\nGPGDir = /custom/gnupg\n[core]\nServer = file:///a\n");
  const auto parsed = parsePacmanRepositories(withGpg, "x86_64");
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->gpg_directory, "/custom/gnupg");

  EXPECT_FALSE(parsePacmanRepositories(tree.write("m.conf", "[core]\nInclude = absent-list\n"), "x86_64").has_value());
  EXPECT_FALSE(parsePacmanRepositories(tree.write("n.conf", "[core]\n"), "x86_64").has_value());
  EXPECT_FALSE(
      parsePacmanRepositories(tree.write("d.conf", "[core]\nServer=file:///a\n[core]\nServer=file:///b\n"), "x86_64")
          .has_value());
  EXPECT_FALSE(
      parsePacmanRepositories(tree.write("absent/none.conf", "").parent_path() / "nope.conf", "x86_64").has_value());
}

}  // namespace
}  // namespace holonight_packages_backends
