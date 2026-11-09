#pragma once

#include "holonight_packages_transaction/transaction_config.h"
#include "package_spec.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace holonight_transaction_testing {

struct RepoConf {
  std::string name;
  std::vector<std::string> servers;
  std::optional<std::string> sigLevel;  // the repository's own SigLevel line, if any
};

struct ConfSpec {
  std::optional<std::string> globalSigLevel = "Never";  // [options] SigLevel; nullopt writes none
  std::vector<std::string> extraOptions;                // raw lines appended to [options]
  std::vector<RepoConf> repos;
};

// A complete offline world under a fresh directory in the system temp dir (REQ-C-009):
//   root/      the installation root that libalpm extracts into
//   db/        local/ and sync/ databases
//   cache/     the package cache; hooks/ the hook directory; gnupg/ the libalpm keyring directory
//   repos/<n>/ the file:// repositories (package files and <n>.db)
//   pacman.conf, written only through writeConf(), which accepts nothing but file:// servers inside this world
// Constructing it with a root that is "/" or outside the temp dir aborts the test process.
class FixtureRoot {
 public:
  FixtureRoot();
  ~FixtureRoot();
  FixtureRoot(const FixtureRoot&) = delete;
  FixtureRoot& operator=(const FixtureRoot&) = delete;
  FixtureRoot(FixtureRoot&&) = delete;
  FixtureRoot& operator=(FixtureRoot&&) = delete;

  // Why `candidate` is not an acceptable world directory, or nullopt. `tempBase` is the allowed parent.
  [[nodiscard]] static std::optional<std::string> rejectRoot(const std::filesystem::path& candidate,
                                                             const std::filesystem::path& tempBase);
  // Why `url` is not an acceptable repository server for the world at `base`, or nullopt.
  [[nodiscard]] static std::optional<std::string> rejectServer(const std::string& url,
                                                               const std::filesystem::path& base);

  [[nodiscard]] const std::filesystem::path& base() const { return base_; }
  [[nodiscard]] std::filesystem::path installRoot() const { return base_ / "root"; }
  [[nodiscard]] std::filesystem::path dbPath() const { return base_ / "db"; }
  [[nodiscard]] std::filesystem::path localDbPath() const { return base_ / "db" / "local"; }
  [[nodiscard]] std::filesystem::path syncDbPath() const { return base_ / "db" / "sync"; }
  [[nodiscard]] std::filesystem::path cacheDir() const { return base_ / "cache"; }
  [[nodiscard]] std::filesystem::path hookDir() const { return base_ / "hooks"; }
  [[nodiscard]] std::filesystem::path gpgDir() const { return base_ / "gnupg"; }
  [[nodiscard]] std::filesystem::path repoDir(const std::string& name) const { return base_ / "repos" / name; }
  [[nodiscard]] std::filesystem::path confPath() const { return base_ / "pacman.conf"; }
  [[nodiscard]] std::filesystem::path lockPath() const { return base_ / "db" / "db.lck"; }

  // The configuration the transaction core runs with against this world.
  [[nodiscard]] holonight_packages_transaction::TransactionConfig config() const;

  // `file://` URL of a repository directory in this world.
  [[nodiscard]] std::string serverUrl(const std::string& repoName) const;

  // Writes pacman.conf. Aborts on a server that is not a file:// URL inside this world.
  void writeConf(const ConfSpec& spec) const;
  // A world with the usual options and one SigLevel for every repository.
  void writeConf(const std::vector<std::string>& repoNames, const std::string& sigLevel = "Never") const;
  // Raw text for tests that need a hand-made file; still checked for non-file servers.
  void writeConfText(const std::string& text) const;

  // Registers `spec` as installed: local database entry plus its files under the install root.
  void installPackage(const PackageSpec& spec, InstallReason reason = InstallReason::Explicit) const;
  // name -> version of every package in the local database.
  [[nodiscard]] std::map<std::string, std::string> installedVersions() const;
  [[nodiscard]] bool isInstalled(const std::string& name) const;
  [[nodiscard]] bool fileExists(const std::string& pathUnderRoot) const;
  [[nodiscard]] std::string readFile(const std::string& pathUnderRoot) const;

  void writeHook(const std::string& fileName, const std::string& text) const;

 private:
  std::filesystem::path base_;
};

}  // namespace holonight_transaction_testing
