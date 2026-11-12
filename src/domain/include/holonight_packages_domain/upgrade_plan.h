#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace holonight_packages_domain {

// The preview of a system upgrade (system-upgrade REQ-F-019). Every list is sorted by its name field.

// An installed package that the upgrade moves to a newer version.
struct UpgradeEntry {
  std::string name;
  std::string old_version;
  std::string new_version;
  std::string repository;

  bool operator==(const UpgradeEntry&) const = default;
};

// An installed package that a sync package replaces (`replaces=`), accepted by the upgrade (REQ-F-020).
struct ReplacementEntry {
  std::string old_name;
  std::string old_version;
  std::string new_name;
  std::string new_version;
  std::string repository;

  bool operator==(const ReplacementEntry&) const = default;
};

// A package that is not installed and that the upgrade installs as a new dependency.
struct NewDependencyEntry {
  std::string name;
  std::string version;
  std::string repository;

  bool operator==(const NewDependencyEntry&) const = default;
};

// An installed package that has an update but stays at its version because of IgnorePkg or IgnoreGroup (REQ-F-021).
struct HeldBackEntry {
  std::string name;
  std::string installed_version;

  bool operator==(const HeldBackEntry&) const = default;
};

struct UpgradeDetail {
  std::vector<UpgradeEntry> upgrades;                // sorted by name
  std::vector<ReplacementEntry> replacements;        // sorted by new_name
  std::vector<NewDependencyEntry> new_dependencies;  // sorted by name
  std::vector<HeldBackEntry> held_back;              // sorted by name
  // archlinux-keyring and the dependencies the plan must add for it; empty means no keyring step (REQ-F-023).
  std::vector<std::string> keyring_packages;  // sorted
  std::uint64_t download_bytes = 0;           // volatile: not part of plan equality (REQ-F-025)
  std::int64_t size_change_bytes = 0;         // volatile

  [[nodiscard]] bool keyring() const noexcept { return !keyring_packages.empty(); }
  [[nodiscard]] std::uint8_t stepCount() const noexcept { return keyring() ? 2 : 1; }
  [[nodiscard]] bool empty() const noexcept {
    return upgrades.empty() && replacements.empty() && new_dependencies.empty();
  }
};

// One live sync database file as the guard saw it after the scratch sync (REQ-F-051).
struct SyncFileStamp {
  std::string name;  // file name in sync/, for example "core.db" or "core.db.sig"
  std::string sha256;
  std::uint64_t size = 0;
  std::int64_t mtime_ns = 0;

  bool operator==(const SyncFileStamp&) const = default;
};

// What an upgrade plan keeps between PrepareUpgrade and Execute, never sent to a caller (REQ-F-008, REQ-F-012).
struct UpgradeWorkspace {
  std::filesystem::path scratch_root;
  std::vector<SyncFileStamp> guard;
};

}  // namespace holonight_packages_domain
