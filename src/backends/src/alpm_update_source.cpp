#include "holonight_packages_backends/alpm_update_source.h"

#include "holonight_packages_persistence/alpm_connection_cache.h"
#include "pacman_config.h"
#include "sync_database_files.h"
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
    return std::unexpected(databaseError(oldest.error()));
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
  if (const auto local = checkLocalDatabase(options_.databasePath); !local.has_value()) {
    return std::unexpected(databaseError(local.error()));
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
