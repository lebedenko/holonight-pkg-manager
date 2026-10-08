#include "holonight_packages_application/update_status.h"

#include "holonight_packages_application/update_summary.h"

#include <chrono>
#include <expected>
#include <optional>

namespace holonight_packages_application {

using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceError;

UpdateStatus buildUpdateStatus(const std::expected<UpdateSnapshot, UpdateSourceError>& result,
                               const std::optional<UpdateStatus>& previous) {
  UpdateStatus status = previous.value_or(UpdateStatus{});
  if (!result) {
    status.state = UpdateState::Error;
    status.lastError = result.error().message;
    return status;
  }

  status.lastError.clear();
  if (!result->databasesFound) {
    status = UpdateStatus{};
    status.state = UpdateState::NoDatabases;
    return status;
  }

  const UpdateSummary summary = summarizeUpdates(result->updates);
  status.state = UpdateState::Ready;
  status.updateCount = summary.updateCount;
  status.ignoredCount = summary.ignoredCount;
  status.totalDownloadBytes = summary.totalDownloadBytes;
  status.dataAsOfEpochSeconds =
      std::chrono::duration_cast<std::chrono::seconds>(result->dataAsOf.time_since_epoch()).count();
  return status;
}

}  // namespace holonight_packages_application
