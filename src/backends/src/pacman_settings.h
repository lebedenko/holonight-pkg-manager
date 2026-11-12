#pragma once

#include "pacman_config.h"
#include "pacman_repositories.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace holonight_packages_backends {

// Everything the privileged helper takes from pacman.conf: the [options] subset and the repositories with their
// signature levels. DBPath, RootDir, XferCommand and every other key are deliberately not read: the helper's paths
// come from its own command line, and it never runs an external download command (REQ-F-018, REQ-F-019).
struct PacmanSettings {
  PacmanConfig options;
  PacmanRepositories repositories;
};

// Fails closed: an unreadable or unparseable file, Include or SigLevel is an error, never a weaker configuration.
// `machine_architecture` resolves "auto".
[[nodiscard]] std::expected<PacmanSettings, std::string> loadPacmanSettings(const std::filesystem::path& path,
                                                                            std::string_view machine_architecture);
// The same, and fills `diagnostics` (when not null) for the system upgrade's configuration checks.
[[nodiscard]] std::expected<PacmanSettings, std::string> loadPacmanSettings(const std::filesystem::path& path,
                                                                            std::string_view machine_architecture,
                                                                            PacmanDiagnostics* diagnostics);

}  // namespace holonight_packages_backends
