#include "holonight_packages_application/orphan_package_filter.h"

#include "holonight_packages_domain/package.h"

#include <gtest/gtest.h>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;

TEST(OrphanPackageFilter, ExplicitPackageIsNotOrphanRegardlessOfRequiredBy) {
  Package package = {.installReason = InstallReason::Explicit, .requiredBy = {}};

  EXPECT_FALSE(isOrphan(package));
}

TEST(OrphanPackageFilter, DependencyPackageWithRequiredByIsNotOrphan) {
  Package package = {.installReason = InstallReason::Dependency, .requiredBy = {"apple"}};

  EXPECT_FALSE(isOrphan(package));
}

TEST(OrphanPackageFilter, DependencyPackageWithoutRequiredByIsOrphan) {
  Package package = {.installReason = InstallReason::Dependency, .requiredBy = {}};

  EXPECT_TRUE(isOrphan(package));
}

TEST(OrphanPackageFilter, ComputeOrphanStatisticsAggregatesCountAndSizeOverFivePackages) {
  const std::vector<Package> packages = {
      Package{.installReason = InstallReason::Explicit, .sizeBytes = 100, .requiredBy = {}},
      Package{.installReason = InstallReason::Explicit, .sizeBytes = 200, .requiredBy = {}},
      Package{.installReason = InstallReason::Dependency, .sizeBytes = 300, .requiredBy = {"apple"}},
      Package{.installReason = InstallReason::Dependency, .sizeBytes = 400, .requiredBy = {}},
      Package{.installReason = InstallReason::Dependency, .sizeBytes = 500, .requiredBy = {}},
  };

  const OrphanStatistics statistics = computeOrphanStatistics(packages);

  EXPECT_EQ(statistics.orphanPackageCount, 2);
  EXPECT_EQ(statistics.reclaimableSizeBytes, 900U);
}

}  // namespace
}  // namespace holonight_packages_application
