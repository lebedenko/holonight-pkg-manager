#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace holonight_packages_transaction {

// Hooks for tests. Production code never sets them: main.cpp of the helper does not reference this header (H-008).
struct TransactionSeams {
  // Called at named points of an execution, on the thread that runs it:
  //   "opened"       the libalpm handle is open
  //   "resolved"     the re-resolution finished and holds the database lock
  //   "downloading"  libalpm starts to retrieve packages (inside the commit, before the commit point)
  //   "commit-point" libalpm reached the first PreTransaction hook or the transaction start
  //   "committed"    the commit gate was passed: a cancel is now refused (REQ-F-058)
  //   "finished"     the transaction was released
  std::function<void(std::string_view stage)> observe;

  // Reports a libalpm error number (a non-zero `alpm_errno_t`) at a named stage, as if libalpm had failed there; zero
  // lets the run go on. Used to test the failure paths that cannot be provoked from outside. Consulted at "resolved"
  // (before the commit) and at "package-done" (after each package change; a fault there stops the remaining packages
  // and ends the run as a failure after a change).
  std::function<int(std::string_view stage)> inject_fault;

  // Replaces libalpm's downloader for the files of one execution: copy `url` into `destinationDirectory` and return 0,
  // return 1 when the file is up to date, or -1 on failure. A test can block here to hold a transaction in its download
  // phase. Unset in production (REQ-F-027, H-004).
  std::function<int(const std::string& url, const std::string& destinationDirectory, bool force)> fetch;

  // system-upgrade. Further observe() stages of an upgrade, in order: "alpm-initialize" (before alpm_initialize),
  // "upgrade-fetch", "fetched", "lock-taken", "guard-checked", "promotion-starting", "promotion-done", "keyring-step",
  // "main-step". Further inject_fault() stages: "promotion-rename-<k>" (before the k-th file, from 1, is replaced),
  // "keyring-commit" and "main-commit" (instead of committing the step).

  // Replaces statvfs for the disk-space estimate: the free bytes of the filesystem holding `path`.
  std::function<std::optional<std::uint64_t>(const std::filesystem::path& path)> free_space;
  // Promotes by copy-then-rename even when scratch and live are on one filesystem (REQ-F-077).
  bool promote_by_copy = false;
};

}  // namespace holonight_packages_transaction
