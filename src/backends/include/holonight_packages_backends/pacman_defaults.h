#pragma once

#include <string_view>

namespace holonight_packages_backends {

// The system pacman locations used by production wiring. Adapters never default to these; callers pass them in.
inline constexpr std::string_view kDefaultPacmanRoot = "/";
inline constexpr std::string_view kDefaultPacmanDatabasePath = "/var/lib/pacman";
inline constexpr std::string_view kDefaultPacmanConfPath = "/etc/pacman.conf";

}  // namespace holonight_packages_backends
