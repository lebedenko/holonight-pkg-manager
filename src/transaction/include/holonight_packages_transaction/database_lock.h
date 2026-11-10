#pragma once

#include <mutex>

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

 private:
  std::unique_lock<std::mutex> lock_;
};

}  // namespace holonight_packages_transaction
