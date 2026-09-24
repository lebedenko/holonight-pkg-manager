#include "holonight_packages_application/update_summary.h"

#include <algorithm>
#include <span>
#include <vector>

namespace holonight_packages_application {

using holonight_packages_domain::PendingUpdate;

UpdateSummary summarizeUpdates(std::span<const PendingUpdate> updates) {
  UpdateSummary summary;
  for (const PendingUpdate& update : updates) {
    if (update.ignored) {
      ++summary.ignoredCount;
      continue;
    }
    ++summary.updateCount;
    summary.totalDownloadBytes += update.downloadSizeBytes;
  }
  return summary;
}

void sortUpdatesByName(std::vector<PendingUpdate>& updates) {
  std::ranges::stable_sort(updates, {}, &PendingUpdate::name);
}

}  // namespace holonight_packages_application
