#pragma once

#include "holonight_packages_application/package_list_refresh.h"

namespace holonight_packages_testing {

// Counts refresh requests (REQ-F-019).
class FakePackageListRefresh final : public holonight_packages_application::PackageListRefresh {
 public:
  FakePackageListRefresh() = default;
  FakePackageListRefresh(const FakePackageListRefresh&) = delete;
  FakePackageListRefresh& operator=(const FakePackageListRefresh&) = delete;
  FakePackageListRefresh(FakePackageListRefresh&&) = delete;
  FakePackageListRefresh& operator=(FakePackageListRefresh&&) = delete;
  ~FakePackageListRefresh() override = default;

  void refreshAfterOutcome() override { ++count_; }
  void refreshAfterUpgrade() override { ++upgradeCount_; }
  [[nodiscard]] int count() const { return count_; }
  [[nodiscard]] int upgradeCount() const { return upgradeCount_; }

 private:
  int count_ = 0;
  int upgradeCount_ = 0;
};

}  // namespace holonight_packages_testing
