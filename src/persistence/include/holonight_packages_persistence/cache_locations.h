#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace holonight_packages_persistence {

using EnvLookup = std::function<std::optional<std::string>(std::string_view name)>;

// $XDG_CACHE_HOME/holonight-packages, else $HOME/.cache/holonight-packages. An empty variable counts as unset.
// nullopt when neither variable is usable.
[[nodiscard]] std::optional<std::filesystem::path> appCacheDir(const EnvLookup& env);

// Looks variables up in the process environment.
[[nodiscard]] EnvLookup processEnvironment();

inline constexpr std::string_view kSnapshotFileName = "update-snapshot.json";
inline constexpr std::string_view kScratchDirectoryName = "checkdb";

}  // namespace holonight_packages_persistence
