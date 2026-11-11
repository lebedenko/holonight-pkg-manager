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
};

}  // namespace holonight_packages_application
