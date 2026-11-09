#pragma once

#include <filesystem>

namespace holonight_packages_transaction {

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
};

}  // namespace holonight_packages_transaction
