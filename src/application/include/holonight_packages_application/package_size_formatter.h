#pragma once

#include <cstdint>
#include <string>

namespace holonight_packages_application {

// Binary (IEC) units: "0 B", "234 KiB", "1.2 MiB", "12.6 GiB". Trailing ".0" is trimmed
// (e.g. exactly 186.0 MiB renders as "186 MiB").
[[nodiscard]] std::string formatSizeBytes(std::uint64_t bytes);

}  // namespace holonight_packages_application
