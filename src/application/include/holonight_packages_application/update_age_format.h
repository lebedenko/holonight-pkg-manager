#pragma once

#include <chrono>
#include <string>

namespace holonight_packages_application {

// Buckets the age of a snapshot for the status line: under 1 min "just now", under 1 h "N min", under 48 h "N h",
// otherwise "N d". A time in the future counts as "just now". The result is untranslated; callers wrap it in tr().
[[nodiscard]] std::string formatSnapshotAge(std::chrono::system_clock::time_point now,
                                            std::chrono::system_clock::time_point fetchedAt);

}  // namespace holonight_packages_application
