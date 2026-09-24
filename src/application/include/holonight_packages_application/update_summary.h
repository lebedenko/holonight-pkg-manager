#pragma once

#include "holonight_packages_domain/pending_update.h"

#include <cstdint>
#include <span>
#include <vector>

namespace holonight_packages_application {

struct UpdateSummary {
  // Rows not flagged as ignored.
  int updateCount = 0;
  int ignoredCount = 0;
  // Sum of download sizes of rows not flagged as ignored.
  std::uint64_t totalDownloadBytes = 0;

  bool operator==(const UpdateSummary&) const = default;
};

// Ignored rows count toward ignoredCount only, never toward updateCount or totalDownloadBytes.
[[nodiscard]] UpdateSummary summarizeUpdates(std::span<const holonight_packages_domain::PendingUpdate> updates);

// Sorts by name ascending; ties (same name from different sources) keep their relative order.
void sortUpdatesByName(std::vector<holonight_packages_domain::PendingUpdate>& updates);

}  // namespace holonight_packages_application
