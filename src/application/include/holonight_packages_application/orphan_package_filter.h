#pragma once

#include "holonight_packages_domain/package.h"

#include <cstdint>
#include <vector>

namespace holonight_packages_application {

[[nodiscard]] bool isOrphan(const holonight_packages_domain::Package& package);

struct OrphanStatistics {
  int orphanPackageCount = 0;
  std::uint64_t reclaimableSizeBytes = 0;
};

[[nodiscard]] OrphanStatistics computeOrphanStatistics(const std::vector<holonight_packages_domain::Package>& packages);

}  // namespace holonight_packages_application
