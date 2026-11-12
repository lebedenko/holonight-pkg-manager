#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>

namespace holonight_packages_application {

enum class ScratchRootError : std::uint8_t { CannotCreate };

// Where a system upgrade keeps its private copy of the sync databases (system-upgrade DESIGN 4.3, REQ-F-008). The
// helper's implementation creates root-owned directories under its systemd state directory; tests use a fake. The path
// never leaves the helper.
class ScratchRoots {
 public:
  ScratchRoots() = default;
  ScratchRoots(const ScratchRoots&) = delete;
  ScratchRoots& operator=(const ScratchRoots&) = delete;
  ScratchRoots(ScratchRoots&&) = delete;
  ScratchRoots& operator=(ScratchRoots&&) = delete;
  virtual ~ScratchRoots();

  // A new, empty directory, owned by root, mode 0711.
  [[nodiscard]] virtual std::expected<std::filesystem::path, ScratchRootError> create() = 0;
  // Removes `root` and everything below it without following symlinks. Idempotent.
  virtual void remove(const std::filesystem::path& root) noexcept = 0;
  // Removes every scratch root, for the start-up sweep (REQ-F-015).
  virtual void removeAll() noexcept = 0;
};

}  // namespace holonight_packages_application
