#include "holonight_packages_backends/alpm_update_source.h"

#include "holonight_packages_persistence/alpm_connection_cache.h"
#include "pacman_config.h"
#include "update_matching.h"

#include <alpm.h>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace holonight_packages_backends {

namespace {

using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceError;
using holonight_packages_domain::UpdateSourceErrorCode;

UpdateSourceError databaseError(std::string message) {
  return UpdateSourceError{.code = UpdateSourceErrorCode::DatabaseOpenFailed, .message = std::move(message)};
}

// Returns the mtime of the oldest {database_path}/sync/*.db, or nullopt when the directory is missing, is not a
// directory, or holds no databases. Those cases are the "no databases" state rather than errors; real I/O failures
// (for example permission denied) are errors.
std::expected<std::optional<std::filesystem::file_time_type>, UpdateSourceError> oldestSyncDatabaseTime(
    const std::filesystem::path& database_path) {
  const auto sync_dir = database_path / "sync";
  std::error_code status_error;
  const auto status = std::filesystem::status(sync_dir, status_error);
  if (status_error && status.type() != std::filesystem::file_type::not_found) {
    return std::unexpected(databaseError("Failed to inspect the sync database directory: " + status_error.message()));
  }
  if (status.type() != std::filesystem::file_type::directory) {
    return std::nullopt;
  }

  std::optional<std::filesystem::file_time_type> oldest;
  std::error_code iterate_error;
  const std::filesystem::directory_iterator end;
  for (auto it = std::filesystem::directory_iterator(sync_dir, iterate_error); !iterate_error && it != end;
       it.increment(iterate_error)) {
    if (it->path().extension() != ".db") {
      continue;
    }
    std::error_code time_error;
    const auto modified = std::filesystem::last_write_time(it->path(), time_error);
    if (time_error) {
      return std::unexpected(databaseError("Failed to read the modification time of " + it->path().string() + ": " +
                                           time_error.message()));
    }
    if (!oldest.has_value() || modified < *oldest) {
      oldest = modified;
    }
  }
  if (iterate_error) {
    return std::unexpected(databaseError("Failed to enumerate sync databases: " + iterate_error.message()));
  }
  return oldest;
}

std::vector<std::string> groupsOf(alpm_pkg_t* pkg) {
  // alpm_pkg_get_groups returns the package's own cached list; borrowed, not freed.
  std::vector<std::string> groups;
  for (alpm_list_t* node = alpm_pkg_get_groups(pkg); node != nullptr; node = alpm_list_next(node)) {
    groups.emplace_back(static_cast<const char*>(node->data));
  }
  return groups;
}

// First registered sync database containing the package wins, as for pacman's repository order.
std::pair<alpm_pkg_t*, alpm_db_t*> findInSyncDatabases(const char* name, const std::vector<alpm_db_t*>& sync_dbs) {
  for (alpm_db_t* sync_db : sync_dbs) {
    if (alpm_pkg_t* found = alpm_db_get_pkg(sync_db, name); found != nullptr) {
      return {found, sync_db};
    }
  }
  return {nullptr, nullptr};
}

std::string stringOrEmpty(const char* text) { return text != nullptr ? text : ""; }

}  // namespace

AlpmUpdateSource::AlpmUpdateSource(AlpmUpdateSourceOptions options)
    : options_(std::move(options)),
      connection_cache_(std::make_unique<holonight_packages_persistence::AlpmConnectionCache>(options_.databaseRoot,
                                                                                              options_.databasePath)) {}

AlpmUpdateSource::~AlpmUpdateSource() = default;

std::expected<UpdateSnapshot, UpdateSourceError> AlpmUpdateSource::loadUpdates() const {
  const auto oldest = oldestSyncDatabaseTime(options_.databasePath);
  if (!oldest.has_value()) {
    return std::unexpected(oldest.error());
  }
  if (!oldest->has_value()) {
    return UpdateSnapshot{.updates = {}, .databasesFound = false, .dataAsOf = {}};
  }

  const auto config = parsePacmanConfig(options_.pacmanConfPath);
  if (!config.has_value()) {
    return std::unexpected(
        UpdateSourceError{.code = UpdateSourceErrorCode::ConfigurationInvalid, .message = config.error()});
  }

  // alpm_initialize can create local/ and its version marker. Check before opening either handle, even when
  // the sync cache is already populated, so a missing local database is an error rather than an empty result.
  const auto local_dir = options_.databasePath / "local";
  std::error_code local_error;
  if (!std::filesystem::is_directory(local_dir, local_error)) {
    return std::unexpected(databaseError("Cannot read local package database directory " + local_dir.string() + ": " +
                                         (local_error ? local_error.message() : "not a directory")));
  }
  const auto version_file = local_dir / "ALPM_DB_VERSION";
  if (!std::filesystem::is_regular_file(version_file, local_error)) {
    return std::unexpected(databaseError("Cannot read local package database version file " + version_file.string() +
                                         ": " + (local_error ? local_error.message() : "not a regular file")));
  }

  auto connection = connection_cache_->connection();
  if (!connection.has_value()) {
    return std::unexpected(databaseError(std::move(connection.error().message)));
  }

  // The local database is read through a fresh handle so it is never stale; only sync databases are cached.
  alpm_errno_t init_error = ALPM_ERR_OK;
  const std::unique_ptr<alpm_handle_t, holonight_packages_persistence::detail::AlpmHandleDeleter> local_handle(
      alpm_initialize(options_.databaseRoot.c_str(), options_.databasePath.c_str(), &init_error));
  if (!local_handle) {
    return std::unexpected(databaseError(alpm_strerror(init_error)));
  }
  alpm_list_t* pkgcache = alpm_db_get_pkgcache(alpm_get_localdb(local_handle.get()));
  if (pkgcache == nullptr && alpm_errno(local_handle.get()) != ALPM_ERR_OK) {
    return std::unexpected(databaseError(alpm_strerror(alpm_errno(local_handle.get()))));
  }

  UpdateSnapshot snapshot{
      .updates = {}, .databasesFound = true, .dataAsOf = std::chrono::clock_cast<std::chrono::system_clock>(**oldest)};
  for (alpm_list_t* node = pkgcache; node != nullptr; node = alpm_list_next(node)) {
    auto* installed = static_cast<alpm_pkg_t*>(node->data);
    const char* name = alpm_pkg_get_name(installed);
    if (name == nullptr) {
      continue;
    }
    const auto [available, repository] = findInSyncDatabases(name, connection->syncDatabases());
    if (available == nullptr) {
      continue;  // foreign package
    }
    const char* installed_version = alpm_pkg_get_version(installed);
    const char* available_version = alpm_pkg_get_version(available);
    if (installed_version == nullptr || available_version == nullptr ||
        alpm_pkg_vercmp(available_version, installed_version) <= 0) {
      continue;
    }

    const off_t download_size = alpm_pkg_get_size(available);
    const std::vector<std::string> groups = groupsOf(available);
    snapshot.updates.push_back(PendingUpdate{
        .name = name,
        .installedVersion = installed_version,
        .availableVersion = available_version,
        .repository = stringOrEmpty(alpm_db_get_name(repository)),
        .downloadSizeBytes = download_size > 0 ? static_cast<std::uint64_t>(download_size) : 0,
        .installedSizeDeltaBytes = static_cast<std::int64_t>(alpm_pkg_get_isize(available)) -
                                   static_cast<std::int64_t>(alpm_pkg_get_isize(installed)),
        .ignored = isIgnored(name, groups, config->ignorePkgs, config->ignoreGroups),
    });
  }
  return snapshot;
}

}  // namespace holonight_packages_backends
