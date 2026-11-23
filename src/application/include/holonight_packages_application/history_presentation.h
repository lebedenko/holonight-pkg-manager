#pragma once

#include "holonight_packages_domain/history_source.h"

#include <chrono>
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace holonight_packages_application {

// Qt-free text and grouping rules of the History page (transaction-history DESIGN 5.5).

// Resolves a uid to a user name; nullopt when the uid has no passwd entry.
using UserNameResolver = std::function<std::optional<std::string>(std::uint32_t uid)>;

// The default resolver: getpwuid_r.
[[nodiscard]] std::optional<std::string> systemUserName(std::uint32_t uid);

// "Packages", "Packages · <user>" when another user ran it, "Packages · uid N" when that uid has no name, "pacman" or
// "unknown" (REQ-F-027, OQ-H5).
[[nodiscard]] std::string originLabel(const holonight_packages_domain::HistoryOrigin& origin, std::uint32_t current_uid,
                                      const UserNameResolver& resolve_user);

// "<n> <kind>" for every nonzero count, in the order upgraded, installed, downgraded, reinstalled, removed, joined by
// " · " (REQ-F-028, OQ-H3). Empty for a transaction without actions.
[[nodiscard]] std::string summaryText(std::span<const holonight_packages_domain::HistoryAction> actions);

// "Completed", "Interrupted" or "In progress".
[[nodiscard]] std::string_view statusLabel(holonight_packages_domain::HistoryStatus status) noexcept;

// "installed", "upgraded", "downgraded", "reinstalled" or "removed".
[[nodiscard]] std::string_view actionKindLabel(holonight_packages_domain::HistoryActionKind kind) noexcept;

// The calendar day of a time point in the process's local time zone (REQ-F-025).
struct LocalDay {
  int year = 0;
  int month = 0;
  int day = 0;

  auto operator<=>(const LocalDay&) const = default;
};

[[nodiscard]] LocalDay localDay(std::chrono::system_clock::time_point time);

// "YYYY-MM-DD", the grouping key of a day.
[[nodiscard]] std::string dayKey(const LocalDay& day);

// "HH:MM" in local time (REQ-F-015).
[[nodiscard]] std::string timeLabel(std::chrono::system_clock::time_point time);

}  // namespace holonight_packages_application
