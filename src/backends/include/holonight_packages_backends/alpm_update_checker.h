#pragma once

#include "holonight_packages_domain/update_checker.h"

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>

namespace holonight_packages_backends {

struct AlpmUpdateCheckerTestHooks {
  // Runs between "the sync copy is complete" and the second lock check. Empty in production.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::function<void()> afterCopyStarted;
  // Runs on the worker thread after the check body has returned and released its lock and descriptors. Empty in
  // production. Lets a test wait for a detached worker to finish.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::function<void()> onBodyFinished;
};

// Every path the adapter touches comes from here; it holds no default pacman locations.
struct AlpmUpdateCheckerOptions {
  // libalpm root directory.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::filesystem::path databaseRoot;
  // The REAL libalpm database path (read only), containing local/ and sync/.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::filesystem::path databasePath;
  // Repositories, SigLevel, GPGDir, Architecture and Ignore*.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::filesystem::path pacmanConfPath;
  // Private scratch root, for example $XDG_CACHE_HOME/holonight-packages/checkdb. No default inside the adapter.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::filesystem::path scratchRoot;
  // Total bound for one check. libalpm cannot be cancelled, so a check that exceeds it is abandoned on a detached
  // thread and reported as NetworkUnavailable.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::seconds totalBound{120};
  AlpmUpdateCheckerTestHooks hooks;
};

// Refreshes a private copy of the sync databases from the configured repositories and compares it with the installed
// packages. The real database path is only read; no transaction is ever opened and no process is started.
class AlpmUpdateChecker final : public holonight_packages_domain::UpdateChecker {
 public:
  explicit AlpmUpdateChecker(AlpmUpdateCheckerOptions options);
  ~AlpmUpdateChecker() override;

  AlpmUpdateChecker(const AlpmUpdateChecker&) = delete;
  AlpmUpdateChecker& operator=(const AlpmUpdateChecker&) = delete;
  AlpmUpdateChecker(AlpmUpdateChecker&&) = delete;
  AlpmUpdateChecker& operator=(AlpmUpdateChecker&&) = delete;

  [[nodiscard]] std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateCheckError>
  checkForUpdates() const override;

  // Reduces the scratch root to its newest run directory. Call once at service start, before the first check.
  // Skipped silently when a check holds the lock or the root is unusable.
  void sweepStale() const;

 private:
  std::shared_ptr<const AlpmUpdateCheckerOptions> options_;
};

}  // namespace holonight_packages_backends
