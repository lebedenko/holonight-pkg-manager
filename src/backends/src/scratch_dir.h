#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace holonight_packages_backends {

enum class ScratchFailure : std::uint8_t { Busy, Invalid };

struct ScratchError {
  ScratchFailure kind = ScratchFailure::Invalid;
  // Developer-facing; logged only, never shown to users.
  std::string message;
};

// Creates the scratch root with mode 0700 when missing. An existing root must be a real directory (not a symlink),
// owned by the effective uid, and not writable by group or others. Anything else is Invalid.
[[nodiscard]] std::expected<void, ScratchError> prepareScratchRoot(const std::filesystem::path& root);

// Exclusive flock on <root>/check.lock (mode 0600, O_NOFOLLOW), held until the object is destroyed or moved from.
class ScratchLock {
 public:
  ScratchLock() = default;
  ScratchLock(const ScratchLock&) = delete;
  ScratchLock& operator=(const ScratchLock&) = delete;
  ScratchLock(ScratchLock&& other) noexcept;
  ScratchLock& operator=(ScratchLock&& other) noexcept;
  ~ScratchLock();

  // Busy when another descriptor holds the lock.
  [[nodiscard]] static std::expected<ScratchLock, ScratchError> acquire(const std::filesystem::path& root);

 private:
  explicit ScratchLock(int descriptor) : descriptor_(descriptor) {}
  int descriptor_ = -1;
};

inline constexpr const char* kScratchLockName = "check.lock";
inline constexpr const char* kScratchRunPrefix = "run-";

// mkdtemp("<root>/run-XXXXXX"), mode 0700.
[[nodiscard]] std::expected<std::filesystem::path, ScratchError> createRunDirectory(const std::filesystem::path& root);

// Removes every entry of the root except the lock file and the single newest run-* directory (newest by mtime, ties
// broken by name). `also_keep`, when non-empty, is kept as well. Must be called with the lock held.
void sweepScratchRoot(const std::filesystem::path& root, const std::filesystem::path& also_keep = {});

// Removes every run-* directory except `keep`.
void removeOtherRunDirectories(const std::filesystem::path& root, const std::filesystem::path& keep);

// Removes a path without ever following a symlink: a symlink (such as a run directory's `local`) is unlinked, never
// entered. Safe to call on a path that does not exist.
void removeTreeWithoutFollowingLinks(const std::filesystem::path& path);

}  // namespace holonight_packages_backends
