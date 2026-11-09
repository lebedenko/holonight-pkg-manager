#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace holonight_transaction_testing {

// A fixture package: enough metadata for libalpm to resolve it and files to extract.
struct PackageSpec {
  std::string name;
  std::string version = "1.0-1";
  std::string description = "fixture package";
  std::string architecture = "any";
  std::vector<std::string> depends;
  std::vector<std::string> provides;
  std::vector<std::string> conflicts;
  std::vector<std::string> replaces;
  std::vector<std::string> groups;
  // Files that pacman should treat as configuration (kept as .pacsave on removal when modified).
  std::vector<std::string> backup;
  // Path relative to the install root -> content.
  std::map<std::string, std::string> files;

  [[nodiscard]] std::string fileName() const { return name + "-" + version + "-" + architecture + ".pkg.tar.zst"; }
};

// What writing a package file produced; the sync database entry is built from it.
struct BuiltPackage {
  std::filesystem::path path;
  std::string sha256;
  std::uint64_t compressedSize = 0;
  std::uint64_t installedSize = 0;
};

enum class InstallReason : std::uint8_t { Explicit, Dependency };

// Writes `spec` as a .pkg.tar.zst into `directory`. Aborts the test process on I/O failure.
BuiltPackage writePackageFile(const PackageSpec& spec, const std::filesystem::path& directory);

// The text of the sync database `desc` entry for a built package.
std::string syncDescEntry(const PackageSpec& spec, const BuiltPackage& built);

// The text of the local database `desc` and `files` entries for an installed package.
std::string localDescEntry(const PackageSpec& spec, InstallReason reason);
std::string localFilesEntry(const PackageSpec& spec);

}  // namespace holonight_transaction_testing
