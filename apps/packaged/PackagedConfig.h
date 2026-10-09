#pragma once

#include "holonight_packages_application/update_check_policy.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Settings of holonight-packaged, read from $XDG_CONFIG_HOME/holonight/packages.toml through holonight-config.
// The file is optional; a missing file or key silently uses the defaults.
//
//   [updates]
//   check_interval_minutes = 360
namespace packaged_config {

using EnvLookup = std::function<std::optional<std::string>(std::string_view name)>;

struct Settings {
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::minutes checkInterval{360};
  // Human-readable warnings to log once at startup (unusable or clamped values).
  std::vector<std::string> warnings;
};

// HOLONIGHT_PACKAGES_FILE (non-empty) replaces the path; otherwise $XDG_CONFIG_HOME/holonight/packages.toml, else
// $HOME/.config/holonight/packages.toml. nullopt when no variable is usable.
[[nodiscard]] std::optional<std::filesystem::path> configPath(const EnvLookup& env);

[[nodiscard]] EnvLookup processEnvironment();

// The raw interval text comes from the command line when given, else from the file. An absent file or key is not a
// warning; an unreadable file, or a present but unusable value, warns once and uses the default.
[[nodiscard]] Settings load(const std::optional<std::filesystem::path>& path,
                            const std::optional<std::string>& cliCheckIntervalMinutes,
                            const holonight_packages_application::UpdateCheckPolicy& policy);

}  // namespace packaged_config
