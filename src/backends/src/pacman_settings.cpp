#include "pacman_settings.h"

namespace holonight_packages_backends {

std::expected<PacmanSettings, std::string> loadPacmanSettings(const std::filesystem::path& path,
                                                              std::string_view machine_architecture) {
  auto options = parsePacmanConfig(path);
  if (!options) {
    return std::unexpected(options.error());
  }
  auto repositories = parsePacmanRepositories(path, machine_architecture);
  if (!repositories) {
    return std::unexpected(repositories.error());
  }
  return PacmanSettings{.options = std::move(*options), .repositories = std::move(*repositories)};
}

}  // namespace holonight_packages_backends
