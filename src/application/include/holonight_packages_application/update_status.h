#pragma once

#include "holonight_packages_domain/update_source.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace holonight_packages_application {

enum class UpdateState : std::uint8_t { Loading, Ready, NoDatabases, Error };

struct UpdateStatus {
  UpdateState state = UpdateState::Loading;
  // Rows not flagged as ignored.
  // NOLINTNEXTLINE(readability-identifier-naming): mirrors UpdateSummary.
  int updateCount = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): mirrors UpdateSummary.
  int ignoredCount = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): mirrors UpdateSummary.
  std::uint64_t totalDownloadBytes = 0;
  // Oldest sync database mtime in Unix seconds; 0 when unknown.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::int64_t dataAsOfEpochSeconds = 0;
  // Empty unless state is Error.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::string lastError;

  bool operator==(const UpdateStatus&) const = default;
};

// Derives the status from one evaluation. A failure keeps the previous counts, totals and timestamp (when there is a
// previous status) and reports Error with the failure message.
[[nodiscard]] UpdateStatus buildUpdateStatus(const std::expected<holonight_packages_domain::UpdateSnapshot,
                                                                 holonight_packages_domain::UpdateSourceError>& result,
                                             const std::optional<UpdateStatus>& previous);

}  // namespace holonight_packages_application
