#pragma once

#include "holonight_packages_application/scratch_roots.h"

#include <filesystem>

namespace holonight_helper {

// Scratch roots under the helper's systemd state directory (/var/lib/holonight-package-helper): `upgrade-XXXXXX`
// directories made by mkdtemp, owned by the helper's user (root in production), mode 0711 (system-upgrade REQ-F-008,
// SPIKES S-1 f). Only entries named `upgrade-*` directly in the state directory are ever removed, and removal never
// follows a symlink.
class StateDirScratchRoots final : public holonight_packages_application::ScratchRoots {
 public:
  explicit StateDirScratchRoots(std::filesystem::path state_directory);

  [[nodiscard]] std::expected<std::filesystem::path, holonight_packages_application::ScratchRootError> create()
      override;
  void remove(const std::filesystem::path& root) noexcept override;
  void removeAll() noexcept override;

 private:
  [[nodiscard]] bool ours(const std::filesystem::path& root) const;

  std::filesystem::path state_;
};

}  // namespace holonight_helper
