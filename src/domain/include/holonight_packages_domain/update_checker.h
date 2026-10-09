#pragma once

#include "holonight_packages_domain/pending_update.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string_view>

namespace holonight_packages_domain {

enum class UpdateCheckErrorCode : std::uint8_t { NetworkUnavailable, RepositoryUnreachable, Busy, Unknown };

inline constexpr std::array<UpdateCheckErrorCode, 4> kAllUpdateCheckErrorCodes{
    UpdateCheckErrorCode::NetworkUnavailable,
    UpdateCheckErrorCode::RepositoryUnreachable,
    UpdateCheckErrorCode::Busy,
    UpdateCheckErrorCode::Unknown,
};

// One fixed message table: no backend text can reach a message.
[[nodiscard]] std::string_view updateCheckErrorMessage(UpdateCheckErrorCode code) noexcept;
// Stable lower-case token for D-Bus and logs: "network-unavailable", "repository-unreachable", "busy", "unknown".
[[nodiscard]] std::string_view updateCheckErrorName(UpdateCheckErrorCode code) noexcept;

struct UpdateCheckError {
  UpdateCheckErrorCode code = UpdateCheckErrorCode::Unknown;

  [[nodiscard]] std::string_view message() const noexcept { return updateCheckErrorMessage(code); }
  bool operator==(const UpdateCheckError&) const = default;
};

// The result of one successful check, stamped by the service.
struct CheckedSnapshot {
  UpdateSnapshot snapshot;
  // Completion time of the check.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::system_clock::time_point fetchedAt;

  bool operator==(const CheckedSnapshot&) const = default;
};

class UpdateChecker {
 public:
  UpdateChecker() = default;
  UpdateChecker(const UpdateChecker&) = default;
  UpdateChecker(UpdateChecker&&) = default;
  UpdateChecker& operator=(const UpdateChecker&) = default;
  UpdateChecker& operator=(UpdateChecker&&) = default;
  virtual ~UpdateChecker();

  // Refreshes a private copy of the package metadata from the configured repositories and compares it with the
  // installed set. Blocking, network access, never writes system package metadata. Call from a worker thread only.
  [[nodiscard]] virtual std::expected<UpdateSnapshot, UpdateCheckError> checkForUpdates() const = 0;
};

}  // namespace holonight_packages_domain
