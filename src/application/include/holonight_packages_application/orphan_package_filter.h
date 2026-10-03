#pragma once

#include "holonight_packages_domain/package.h"

#include <cstdint>
#include <vector>

namespace holonight_packages_application {

[[nodiscard]] bool isOrphan(const holonight_packages_domain::Package& package);

struct OrphanStatistics {
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  int orphanPackageCount = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::uint64_t reclaimableSizeBytes = 0;
};

[[nodiscard]] OrphanStatistics computeOrphanStatistics(const std::vector<holonight_packages_domain::Package>& packages);

}  // namespace holonight_packages_application
