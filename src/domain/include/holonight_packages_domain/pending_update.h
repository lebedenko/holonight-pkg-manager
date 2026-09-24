#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace holonight_packages_domain {

struct PendingUpdate {
  std::string name;
  std::string installedVersion;
  std::string availableVersion;
  // Sync repository that supplied the newer version.
  std::string repository;
  // Compressed package size of the newer version.
  std::uint64_t downloadSizeBytes = 0;
  // Installed size of the newer version minus the installed size of the current one; may be negative.
  std::int64_t installedSizeDeltaBytes = 0;
  // Matched by pacman.conf IgnorePkg / IgnoreGroup. Informational only.
  bool ignored = false;

  bool operator==(const PendingUpdate&) const = default;
};

struct UpdateSnapshot {
  // Includes ignored rows; order is unspecified.
  std::vector<PendingUpdate> updates;
  // False when the sync directory is missing or holds no databases. Not an error.
  bool databasesFound = true;
  // Modification time of the oldest sync database. Meaningless when databasesFound is false.
  std::chrono::system_clock::time_point dataAsOf;

  bool operator==(const UpdateSnapshot&) const = default;
};

}  // namespace holonight_packages_domain
