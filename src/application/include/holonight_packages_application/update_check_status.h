#pragma once

#include "holonight_packages_domain/update_checker.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace holonight_packages_application {

enum class CheckOrigin : std::uint8_t { Automatic, OnDemand };

struct UpdateCheckOutcome {
  bool succeeded = false;
  // Set if and only if the check failed.
  std::optional<holonight_packages_domain::UpdateCheckErrorCode> error;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::system_clock::time_point completedAt;
  CheckOrigin origin = CheckOrigin::Automatic;

  bool operator==(const UpdateCheckOutcome&) const = default;
};

struct UpdateCheckStatus {
  bool checking = false;
  // False until the first check completes.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool hasCheckResult = false;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool lastCheckSucceeded = false;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::optional<holonight_packages_domain::UpdateCheckErrorCode> lastError;
  // Meaningful if and only if hasCheckResult.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::system_clock::time_point lastCheckTime;
  // nullopt: no snapshot yet.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::optional<std::chrono::system_clock::time_point> snapshotFetchedAt;
  // Non-ignored update count of the last-good snapshot.
  std::optional<int> count;

  bool operator==(const UpdateCheckStatus&) const = default;
};

}  // namespace holonight_packages_application
