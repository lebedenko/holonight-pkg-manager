#pragma once

#include <chrono>

namespace holonight_packages_application {

// Sync data older than this (strictly) shows the stale-database hint.
inline constexpr std::chrono::days kStaleAfter{7};

[[nodiscard]] constexpr bool isStale(std::chrono::system_clock::time_point dataAsOf,
                                     std::chrono::system_clock::time_point now) {
  return now - dataAsOf > kStaleAfter;
}

}  // namespace holonight_packages_application
