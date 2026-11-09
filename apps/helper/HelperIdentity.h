#pragma once

// The names that identify the helper on the system bus and in polkit. The introspection XML, the polkit policy, the bus
// policy, the activation file and the systemd unit all carry the same strings; scripts/check-helper-artifacts.py keeps
// them in agreement (REQ-C-005).
#include <string_view>

namespace holonight_helper {

inline constexpr std::string_view kBusName = "org.holonight.PackageHelper1";
inline constexpr std::string_view kObjectPath = "/org/holonight/PackageHelper1";
inline constexpr std::string_view kInterfaceName = "org.holonight.PackageHelper1.Transactions";
inline constexpr std::string_view kInstallActionId = "org.holonight.packages.install";
inline constexpr std::string_view kRemoveActionId = "org.holonight.packages.remove";
inline constexpr std::string_view kUnitName = "holonight-package-helper.service";
inline constexpr unsigned kProtocolVersion = 1;

}  // namespace holonight_helper
