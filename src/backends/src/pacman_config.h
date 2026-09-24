#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace holonight_packages_backends {

// The subset of pacman.conf the update check needs. Every other key and section is ignored.
struct PacmanConfig {
  // [options] IgnorePkg, accumulated over all lines, whitespace-split. Entries may be fnmatch(3) globs.
  std::vector<std::string> ignorePkgs;
  // [options] IgnoreGroup, same rules as ignorePkgs.
  std::vector<std::string> ignoreGroups;

  bool operator==(const PacmanConfig&) const = default;
};

// Parses `[section]` headers, `key = value` lines and `#` comments (whole-line and trailing). There is no Include
// support. A missing or unreadable file is an error, never an empty config: an empty ignore list would silently
// inflate the update count.
[[nodiscard]] std::expected<PacmanConfig, std::string> parsePacmanConfig(const std::filesystem::path& path);

}  // namespace holonight_packages_backends
