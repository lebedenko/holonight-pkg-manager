#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace holonight_packages_domain {

// One package as it appears in one sync database. Distinct from the installed Package: no install date, reason,
// required-by or config-file data, and it carries sync-only metadata (URL, licenses, dependencies, download size).
struct SyncPackage {
  std::string name;
  // Version offered by the repository.
  std::string version;
  // Sync database name (file stem), e.g. "core".
  std::string repository;
  // Empty when the database has none.
  std::string description;
  // Upstream URL as stored; empty when absent.
  std::string url;
  std::vector<std::string> licenses;
  // Dependency string form, e.g. "glibc>=2.38".
  std::vector<std::string> dependencies;
  // "name: reason" form.
  std::vector<std::string> optionalDependencies;
  // Compressed package size.
  std::uint64_t downloadSizeBytes = 0;
  std::uint64_t installedSizeBytes = 0;
  // Version of the same-named package in the local database; nullopt when not installed. Filled by the adapter from
  // the local database read in the same load, so it is as fresh as the load.
  std::optional<std::string> installedVersion;

  bool operator==(const SyncPackage&) const = default;
};

struct ExploreSnapshot {
  // Every (repository, name) entry; order is unspecified.
  std::vector<SyncPackage> packages;
  // False when the sync directory is missing or holds no databases. Not an error.
  bool databasesFound = true;
  // Modification time of the oldest sync database. Meaningless when databasesFound is false.
  std::chrono::system_clock::time_point dataAsOf;

  bool operator==(const ExploreSnapshot&) const = default;
};

}  // namespace holonight_packages_domain
