#pragma once

#include "holonight_packages_domain/package_source.h"

#include <alpm.h>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace holonight_packages_persistence {

namespace detail {
struct AlpmHandleDeleter {
  void operator()(alpm_handle_t* handle) const;
};
}  // namespace detail

// A lease on the cache's ALPM handle and registered sync databases. The lease keeps the cache
// locked, so its pointers remain valid and no other thread can use or invalidate the handle.
class AlpmConnection {
 public:
  AlpmConnection(const AlpmConnection&) = delete;
  AlpmConnection& operator=(const AlpmConnection&) = delete;
  AlpmConnection(AlpmConnection&&) = default;
  AlpmConnection& operator=(AlpmConnection&&) = default;
  ~AlpmConnection() = default;

  [[nodiscard]] alpm_handle_t* handle() const;
  [[nodiscard]] const std::vector<alpm_db_t*>& syncDatabases() const;

 private:
  friend class AlpmConnectionCache;

  AlpmConnection(alpm_handle_t* handle, const std::vector<alpm_db_t*>& sync_databases,
                 std::unique_lock<std::mutex> lock);

  alpm_handle_t* handle_ = nullptr;
  const std::vector<alpm_db_t*>* sync_databases_ = nullptr;
  std::unique_lock<std::mutex> lock_;
};

// Per-instance cache of an ALPM handle and its registered sync databases for one
// (database_root, database_path) pair, kept fresh by comparing {database_path}/sync/*.db mtimes
// and the file list against the values recorded at last population. Not copyable or movable.
//
// Thread-safety: connection() returns a lease that retains the internal mutex. Calls from different
// threads are serialized until the previous lease is destroyed, keeping all borrowed ALPM pointers
// valid for the complete operation without merging calls or sharing their results.
class AlpmConnectionCache {
 public:
  AlpmConnectionCache(std::filesystem::path database_root, std::filesystem::path database_path);
  ~AlpmConnectionCache();

  AlpmConnectionCache(const AlpmConnectionCache&) = delete;
  AlpmConnectionCache& operator=(const AlpmConnectionCache&) = delete;
  AlpmConnectionCache(AlpmConnectionCache&&) = delete;
  AlpmConnectionCache& operator=(AlpmConnectionCache&&) = delete;

  // Returns the cached connection if {database_path}/sync/*.db is unchanged since the last call;
  // otherwise releases the old handle, re-initializes ALPM, re-registers all sync databases, and
  // records the new state before returning it. Errors from alpm_initialize()/registration/sync-
  // directory inspection propagate unchanged from today's behavior (see SPEC REQ-F-009/010).
  [[nodiscard]] std::expected<AlpmConnection, holonight_packages_domain::PackageSourceError> connection();

 private:
  [[nodiscard]] std::expected<AlpmConnection, holonight_packages_domain::PackageSourceError> connectionLocked(
      std::unique_lock<std::mutex> lock);

  std::mutex mutex_;
  std::filesystem::path database_root_;
  std::filesystem::path database_path_;
  std::unique_ptr<alpm_handle_t, detail::AlpmHandleDeleter> handle_;
  std::vector<alpm_db_t*> sync_databases_;
  std::optional<std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>> known_snapshot_;
};

}  // namespace holonight_packages_persistence
