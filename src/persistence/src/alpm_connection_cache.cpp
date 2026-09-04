#include "holonight_packages_persistence/alpm_connection_cache.h"

#include <algorithm>
#include <optional>
#include <string>
#include <system_error>

namespace holonight_packages_persistence {

namespace detail {
void AlpmHandleDeleter::operator()(alpm_handle_t* handle) const {
  if (handle != nullptr) {
    alpm_release(handle);
  }
}
}  // namespace detail

namespace {

using holonight_packages_domain::PackageSourceError;
using holonight_packages_domain::PackageSourceErrorCode;

using SyncSnapshot = std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>;

PackageSourceError databaseError(std::string message) {
  return PackageSourceError{.code = PackageSourceErrorCode::DatabaseOpenFailed, .message = std::move(message)};
}

std::expected<std::vector<std::filesystem::path>, PackageSourceError> listSyncDbFiles(
    const std::filesystem::path& database_path) {
  std::vector<std::filesystem::path> db_paths;
  const auto sync_dir = database_path / "sync";
  std::error_code exists_error;
  const bool sync_dir_exists = std::filesystem::exists(sync_dir, exists_error);
  if (exists_error) {
    return std::unexpected(databaseError("Failed to inspect sync database directory: " + exists_error.message()));
  }
  if (!sync_dir_exists) {
    return db_paths;
  }

  std::error_code type_error;
  if (!std::filesystem::is_directory(sync_dir, type_error) || type_error) {
    const std::string detail = type_error ? type_error.message() : "path is not a directory";
    return std::unexpected(databaseError("Invalid sync database directory: " + detail));
  }

  std::error_code iterate_error;
  const std::filesystem::directory_iterator end;
  for (auto it = std::filesystem::directory_iterator(sync_dir, iterate_error); !iterate_error && it != end;
       it.increment(iterate_error)) {
    if (it->path().extension() != ".db") {
      continue;
    }
    db_paths.push_back(it->path());
  }
  if (iterate_error) {
    return std::unexpected(databaseError("Failed to enumerate sync databases: " + iterate_error.message()));
  }

  std::ranges::sort(db_paths);
  return db_paths;
}

std::optional<SyncSnapshot> snapshotOf(const std::vector<std::filesystem::path>& paths) {
  SyncSnapshot snapshot;
  snapshot.reserve(paths.size());
  try {
    for (const auto& path : paths) {
      snapshot.emplace_back(path, std::filesystem::last_write_time(path));
    }
  } catch (const std::filesystem::filesystem_error&) {
    return std::nullopt;
  }
  return snapshot;
}

std::expected<std::vector<alpm_db_t*>, PackageSourceError> registerSyncDatabases(
    alpm_handle_t* handle, const std::vector<std::filesystem::path>& db_paths) {
  std::vector<alpm_db_t*> sync_dbs;
  sync_dbs.reserve(db_paths.size());
  for (const auto& db_path : db_paths) {
    const std::string repo_name = db_path.stem().string();
    alpm_db_t* sync_db = alpm_register_syncdb(handle, repo_name.c_str(), ALPM_SIG_USE_DEFAULT);
    if (sync_db == nullptr) {
      return std::unexpected(
          databaseError("Failed to register sync database '" + repo_name + "': " + alpm_strerror(alpm_errno(handle))));
    }
    if (alpm_db_get_pkgcache(sync_db) == nullptr && alpm_errno(handle) != ALPM_ERR_OK) {
      return std::unexpected(
          databaseError("Failed to load sync database '" + repo_name + "': " + alpm_strerror(alpm_errno(handle))));
    }
    sync_dbs.push_back(sync_db);
  }
  return sync_dbs;
}

}  // namespace

AlpmConnectionCache::AlpmConnectionCache(std::filesystem::path database_root, std::filesystem::path database_path)
    : database_root_(std::move(database_root)), database_path_(std::move(database_path)) {}

AlpmConnectionCache::~AlpmConnectionCache() = default;

AlpmConnection::AlpmConnection(alpm_handle_t* handle, const std::vector<alpm_db_t*>& sync_databases,
                               std::unique_lock<std::mutex> lock)
    : handle_(handle), sync_databases_(&sync_databases), lock_(std::move(lock)) {}

alpm_handle_t* AlpmConnection::handle() const { return handle_; }

const std::vector<alpm_db_t*>& AlpmConnection::syncDatabases() const { return *sync_databases_; }

std::expected<AlpmConnection, PackageSourceError> AlpmConnectionCache::connection() {
  std::unique_lock<std::mutex> lock(mutex_);
  return connectionLocked(std::move(lock));
}

std::expected<AlpmConnection, PackageSourceError> AlpmConnectionCache::connectionLocked(
    std::unique_lock<std::mutex> lock) {
  const auto listing = listSyncDbFiles(database_path_);
  if (!listing.has_value()) {
    return std::unexpected(listing.error());
  }

  const auto fresh_snapshot = snapshotOf(*listing);
  const bool cache_is_fresh = handle_ != nullptr && fresh_snapshot.has_value() && known_snapshot_.has_value() &&
                              *fresh_snapshot == *known_snapshot_;
  if (cache_is_fresh) {
    return AlpmConnection(handle_.get(), sync_databases_, std::move(lock));
  }

  handle_.reset();
  sync_databases_.clear();
  known_snapshot_.reset();

  alpm_errno_t init_error = ALPM_ERR_OK;
  handle_.reset(alpm_initialize(database_root_.c_str(), database_path_.c_str(), &init_error));
  if (!handle_) {
    const auto code = init_error == ALPM_ERR_NOT_A_DIR ? PackageSourceErrorCode::DatabaseRootInvalid
                                                       : PackageSourceErrorCode::DatabaseOpenFailed;
    return std::unexpected(PackageSourceError{.code = code, .message = alpm_strerror(init_error)});
  }

  auto registered = registerSyncDatabases(handle_.get(), *listing);
  if (!registered.has_value()) {
    handle_.reset();  // REQ-F-010: no partially-registered state survives a failure
    return std::unexpected(std::move(registered.error()));
  }
  sync_databases_ = std::move(*registered);
  const auto final_listing = listSyncDbFiles(database_path_);
  const auto final_snapshot = final_listing.has_value() ? snapshotOf(*final_listing) : std::nullopt;
  if (fresh_snapshot.has_value() && final_listing.has_value() && *final_listing == *listing &&
      final_snapshot.has_value() && *final_snapshot == *fresh_snapshot) {
    known_snapshot_ = *final_snapshot;
  }

  return AlpmConnection(handle_.get(), sync_databases_, std::move(lock));
}

}  // namespace holonight_packages_persistence
