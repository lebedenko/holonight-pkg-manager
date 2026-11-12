#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace holonight_packages_transaction {

// Bounds of the system upgrade's database sync (system-upgrade REQ-F-003, REQ-F-010; values from SPIKES S-4). The
// helper never changes them; tests use small values.
struct UpgradeLimits {
  std::chrono::seconds sync_timeout{480};
  std::uint64_t scratch_cap_bytes = std::uint64_t{256} << 20U;
};
// REQ-F-003: the helper must answer before the documented 10-minute client timeout.
static_assert(UpgradeLimits{}.sync_timeout < std::chrono::seconds(600));

// Where the transaction core reads and writes. The helper builds it from its own command line and nothing a D-Bus
// caller sends (REQ-F-018). Tests point every member into a temporary fixture root.
struct TransactionConfig {
  // The installation root that libalpm extracts into ("/" in production).
  std::filesystem::path root;
  // The libalpm database directory (local and sync databases).
  std::filesystem::path database_path;
  // The pacman configuration file: repositories, signature levels, HoldPkg, IgnorePkg and the like.
  std::filesystem::path pacman_conf_path;
  // Used only when pacman.conf names no CacheDir (REQ-F-019).
  std::filesystem::path cache_directory;
  UpgradeLimits upgrade_limits;
};

}  // namespace holonight_packages_transaction
