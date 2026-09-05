#include "holonight_packages_application/orphan_package_filter.h"

#include "holonight_packages_domain/package.h"

namespace holonight_packages_application {

using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;

bool isOrphan(const Package& package) {
  return package.installReason == InstallReason::Dependency && package.requiredBy.empty();
}

OrphanStatistics computeOrphanStatistics(const std::vector<Package>& packages) {
  OrphanStatistics statistics;
  for (const Package& package : packages) {
    if (isOrphan(package)) {
      ++statistics.orphanPackageCount;
      statistics.reclaimableSizeBytes += package.sizeBytes;
    }
  }
  return statistics;
}

}  // namespace holonight_packages_application
