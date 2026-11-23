#pragma once

#include <chrono>
#include <optional>
#include <string_view>

namespace holonight_packages_backends {

// The text between the brackets of a pacman log timestamp, in one of two shapes (transaction-history REQ-F-015):
//   current: 2026-11-23T10:05:18+0200  (24 characters, explicit UTC offset)
//   legacy:  2019-01-01 12:00          (16 characters, no offset; read as local time of the process TZ)

// Cheap shape check (lengths, separators and digit positions only), used on every line.
[[nodiscard]] bool hasTimestampShape(std::string_view inside_brackets) noexcept;

// Full parse. Returns nullopt for any other shape or an out-of-range field.
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> parseTimestamp(std::string_view inside_brackets);

}  // namespace holonight_packages_backends
