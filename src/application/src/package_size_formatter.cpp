#include "holonight_packages_application/package_size_formatter.h"

#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace holonight_packages_application {

namespace {
constexpr std::array<const char*, 5> kUnits = {"B", "KiB", "MiB", "GiB", "TiB"};
}  // namespace

std::string formatSizeBytes(std::uint64_t bytes) {
  if (bytes < 1024) {
    return std::to_string(bytes) + " B";
  }

  auto value = static_cast<double>(bytes);
  std::size_t unit_index = 0;
  while (value >= 1024.0 && unit_index + 1 < kUnits.size()) {
    value /= 1024.0;
    ++unit_index;
  }

  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << value;
  std::string formatted = stream.str();
  if (formatted.ends_with(".0")) {
    formatted.resize(formatted.size() - 2);
  }
  return formatted + " " + kUnits.at(unit_index);
}

std::string formatSignedSizeBytes(std::int64_t bytes) {
  if (bytes == 0) {
    return formatSizeBytes(0);
  }
  // Negate in unsigned arithmetic so INT64_MIN does not overflow.
  const auto magnitude =
      bytes < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(bytes) : static_cast<std::uint64_t>(bytes);
  return (bytes < 0 ? "-" : "+") + formatSizeBytes(magnitude);
}

}  // namespace holonight_packages_application
