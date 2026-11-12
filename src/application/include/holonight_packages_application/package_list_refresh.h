#pragma once

namespace holonight_packages_application {

// Asks the package lists to re-read themselves from the package database after a transaction outcome (REQ-F-019).
class PackageListRefresh {
 public:
  PackageListRefresh() = default;
  PackageListRefresh(const PackageListRefresh&) = delete;
  PackageListRefresh& operator=(const PackageListRefresh&) = delete;
  PackageListRefresh(PackageListRefresh&&) = delete;
  PackageListRefresh& operator=(PackageListRefresh&&) = delete;
  virtual ~PackageListRefresh() = default;

  // Fire and forget; called exactly once per Succeeded, Failed, Cancelled or Unknown outcome.
  virtual void refreshAfterOutcome() = 0;

  // After every outcome of a system upgrade, refusals and NothingToDo included: re-reads the Installed, Updates and
  // Explore lists and asks holonight-packaged for a new update count (system-upgrade REQ-F-088).
  virtual void refreshAfterUpgrade() = 0;
};

}  // namespace holonight_packages_application
