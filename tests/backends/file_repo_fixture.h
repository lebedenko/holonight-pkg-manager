#pragma once

#include "manifest.h"

#include <QTemporaryDir>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace holonight_packages_testing {

// A complete offline world for one online-check test under a QTemporaryDir:
//   mirror/        core.db and extra.db that a file:// Server serves (newer packages, or nothing new)
//   real/          the "real" dbpath: local/ (installed packages) and sync/ (older databases, old mtimes)
//   cache/         XDG_CACHE_HOME for the test (the scratch root is cache/checkdb)
//   pacman.conf    written only through writeConf(), which refuses anything but file:// servers inside the temp dir
class FileRepoFixture {
 public:
  enum class Mirror : std::uint8_t { Newer, NothingNew };

  explicit FileRepoFixture(Mirror mirror = Mirror::Newer) {
    namespace fs = std::filesystem;
    if (!temp_.isValid()) {
      std::abort();
    }
    root_ = fs::path(temp_.path().toStdString());
    const fs::path updates = fs::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "updates";
    fs::create_directories(root_ / "mirror");
    fs::create_directories(root_ / "real" / "sync");
    fs::create_directories(root_ / "cache");
    fs::copy(updates / "local", root_ / "real" / "local", fs::copy_options::recursive);
    const fs::path mirrorSource = mirror == Mirror::Newer ? updates / "sync" : updates / "sync-old";
    for (const char* repo : {"core.db", "extra.db"}) {
      fs::copy_file(mirrorSource / repo, root_ / "mirror" / repo);
      fs::copy_file(updates / "sync-old" / repo, root_ / "real" / "sync" / repo);
      fs::last_write_time(root_ / "real" / "sync" / repo,
                          fs::file_time_type::clock::now() - std::chrono::hours{24 * 7});
    }
    writeConf({"file://" + (root_ / "mirror").string()});
  }

  [[nodiscard]] const std::filesystem::path& root() const { return root_; }
  [[nodiscard]] std::filesystem::path mirrorDir() const { return root_ / "mirror"; }
  [[nodiscard]] std::filesystem::path realDbPath() const { return root_ / "real"; }
  [[nodiscard]] std::filesystem::path cacheHome() const { return root_ / "cache"; }
  [[nodiscard]] std::filesystem::path scratchRoot() const { return root_ / "cache" / "checkdb"; }
  [[nodiscard]] std::filesystem::path confPath() const { return root_ / "pacman.conf"; }

  // The only way to produce a pacman.conf in tests. Every Server must be file:// and point inside the temp dir.
  void writeConf(const std::vector<std::string>& servers, const std::string& sigLevel = "Never",
                 const std::string& extraOptions = "") const {
    for (const std::string& server : servers) {
      requireFixtureServer(server);
    }
    std::ofstream out(confPath(), std::ios::trunc);
    out << "[options]\nArchitecture = auto\nIgnorePkg = ign*\nIgnoreGroup = fruits\nSigLevel = " << sigLevel << "\n"
        << extraOptions;
    for (const char* repo : {"core", "extra"}) {
      out << "[" << repo << "]\n";
      for (const std::string& server : servers) {
        out << "Server = " << server << "\n";
      }
    }
  }

  [[nodiscard]] bool isFixtureServer(const std::string& server) const {
    const std::string prefix = "file://";
    if (!server.starts_with(prefix)) {
      return false;
    }
    const std::filesystem::path path = std::filesystem::weakly_canonical(server.substr(prefix.size()));
    const std::filesystem::path base = std::filesystem::weakly_canonical(root_);
    const auto relative = path.lexically_relative(base);
    return !relative.empty() && *relative.begin() != "..";
  }

 private:
  void requireFixtureServer(const std::string& server) const {
    if (!isFixtureServer(server)) {
      std::cerr << "FileRepoFixture: refusing Server '" << server << "': only file:// inside the temp dir is allowed\n";
      std::abort();
    }
  }

  QTemporaryDir temp_;
  std::filesystem::path root_;
};

}  // namespace holonight_packages_testing
