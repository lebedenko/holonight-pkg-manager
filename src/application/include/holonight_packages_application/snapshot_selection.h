#pragma once

#include "holonight_packages_domain/update_checker.h"

#include <cstdint>
#include <optional>

namespace holonight_packages_application {

enum class SnapshotChoice : std::uint8_t { Local, Online };

// Evaluated results and explicitly historical fallback rows take precedence over saved online rows.
// Ports without either marker retain the aggregate timestamp rule; a missing side forces the choice.
[[nodiscard]] SnapshotChoice selectFresherSnapshot(
    const std::optional<holonight_packages_domain::UpdateSnapshot>& local,
    const std::optional<holonight_packages_domain::UpdateSnapshot>& online);

// Tracks the latest offline evaluation and the historical rows from a published check.
class UpdateSnapshotState {
 public:
  void recordEvaluation(holonight_packages_domain::UpdateSnapshot snapshot);
  void adoptChecked(const holonight_packages_domain::CheckedSnapshot& snapshot);
  void clearChecked();

  [[nodiscard]] const std::optional<holonight_packages_domain::UpdateSnapshot>& evaluation() const;
  [[nodiscard]] const std::optional<holonight_packages_domain::CheckedSnapshot>& checked() const;
  // Requires a recorded evaluation or an adopted checked snapshot.
  [[nodiscard]] const holonight_packages_domain::UpdateSnapshot& selected() const;

  void invalidateInFlightEvaluation(bool running);
  [[nodiscard]] bool takeInvalidation();

 private:
  std::optional<holonight_packages_domain::UpdateSnapshot> evaluation_;
  std::optional<holonight_packages_domain::CheckedSnapshot> checked_;
  bool invalidated_ = false;
};

}  // namespace holonight_packages_application
