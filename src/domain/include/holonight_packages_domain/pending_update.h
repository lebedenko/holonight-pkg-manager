#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace holonight_packages_domain {

struct PendingUpdate {
  std::string name;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::string installedVersion;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::string availableVersion;
  // Sync repository that supplied the newer version.
  std::string repository;
  // Compressed package size of the newer version.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::uint64_t downloadSizeBytes = 0;
  // Installed size of the newer version minus the installed size of the current one; may be negative.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::int64_t installedSizeDeltaBytes = 0;
  // Matched by pacman.conf IgnorePkg / IgnoreGroup. Informational only.
  bool ignored = false;

  bool operator==(const PendingUpdate&) const = default;
};

struct RepositoryProvenance {
  std::string name;
  std::string identity;
  std::string digest;
  std::chrono::system_clock::time_point timestamp;
  std::filesystem::path database;
  bool checked = false;
  bool operator==(const RepositoryProvenance&) const = default;
};

struct UpdateSnapshot {
  // Includes ignored rows; order is unspecified.
  std::vector<PendingUpdate> updates;
  // False when the sync directory is missing or holds no databases. Not an error.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool databasesFound = true;
  // Modification time of the oldest sync database. Meaningless when databasesFound is false.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::system_clock::time_point dataAsOf;

  // Internal only: never included in the public snapshot JSON schema.
  std::vector<RepositoryProvenance> repositories;
  bool evaluated = false;
  bool previously_loaded = false;

  bool operator==(const UpdateSnapshot&) const = default;
};

}  // namespace holonight_packages_domain
