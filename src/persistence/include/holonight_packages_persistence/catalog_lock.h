#pragma once

#include <fcntl.h>
#include <filesystem>
#include <sys/file.h>
#include <unistd.h>

namespace holonight_packages_persistence {

// Readers hold this lease through evaluation; publication and reclamation take an exclusive lease.
class CatalogLock {
 public:
  explicit CatalogLock(const std::filesystem::path& snapshot, bool writer = false) {
    const auto file = snapshot.parent_path() / ".catalog.lock";
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
    descriptor_ = ::open(file.c_str(), O_NOFOLLOW | O_CLOEXEC | (writer ? O_CREAT | O_RDWR : O_RDONLY), 0600);
    if (descriptor_ >= 0 && ::flock(descriptor_, writer ? LOCK_EX : LOCK_SH) != 0) {
      ::close(descriptor_);
      descriptor_ = -1;
    }
  }
  CatalogLock(const CatalogLock&) = delete;
  CatalogLock& operator=(const CatalogLock&) = delete;
  CatalogLock(CatalogLock&&) = delete;
  CatalogLock& operator=(CatalogLock&&) = delete;
  ~CatalogLock() {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
  }
  [[nodiscard]] bool valid() const { return descriptor_ >= 0; }

 private:
  int descriptor_ = -1;
};

}  // namespace holonight_packages_persistence
