#pragma once

#include <filesystem>
#include <mutex>
#include <optional>

namespace holonight_packages_transaction {

// Serializes forced cancellation with all libalpm operations that acquire or release db.lck.
// Production finalization must keep this guard alive until _Exit(), including after removing the lock.
class DatabaseLockGuard final {
 public:
  DatabaseLockGuard();
  ~DatabaseLockGuard() = default;
  DatabaseLockGuard(const DatabaseLockGuard&) = delete;
  DatabaseLockGuard& operator=(const DatabaseLockGuard&) = delete;
  DatabaseLockGuard(DatabaseLockGuard&&) = delete;
  DatabaseLockGuard& operator=(DatabaseLockGuard&&) = delete;

  [[nodiscard]] bool ownsLock() const noexcept;
  // The lock file of the session that owns the lock: the live <dbpath>/db.lck for an install, remove or upgrade
  // commit, or the scratch one during an upgrade sync or fetch. Finalization removes this path and no other.
  [[nodiscard]] std::optional<std::filesystem::path> ownedLockPath() const;

 private:
  std::unique_lock<std::mutex> lock_;
};

}  // namespace holonight_packages_transaction
