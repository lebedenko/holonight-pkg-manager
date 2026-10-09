#pragma once

#include "holonight_packages_application/update_check_ports.h"

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace holonight_packages_application {

struct UpdateCheckPolicy {
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::seconds startupDelay{60};
  std::chrono::minutes interval{360};
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::minutes maxJitter{10};
  std::array<std::chrono::minutes, 3> backoff{
      std::chrono::minutes{5},
      std::chrono::minutes{15},
      std::chrono::minutes{60},
  };
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::minutes minInterval{15};
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::seconds onDemandCooldown{10};
};

// consecutiveFailures == 0 gives interval +/- jitter; 1..3 gives backoff[n-1] exactly (no jitter); 4 or more gives
// interval +/- jitter. The jitter bound is min(maxJitter, interval / 10).
[[nodiscard]] std::chrono::milliseconds nextAutomaticDelay(const UpdateCheckPolicy& policy, int consecutiveFailures,
                                                           RandomSource& random);

struct IntervalResolution {
  std::chrono::minutes interval;
  // Exactly one warning text when the input was unusable.
  std::optional<std::string> warning;
};

// nullopt (file or key missing): default interval, no warning. Present but empty, non-numeric or not positive:
// default interval plus one warning. Below minInterval or above the timer-safe bound: clamped plus one warning.
// The default policy maximum is 35,781 minutes, including maximum positive jitter.
[[nodiscard]] IntervalResolution resolveCheckInterval(std::optional<std::string_view> raw,
                                                      const UpdateCheckPolicy& policy);

}  // namespace holonight_packages_application
