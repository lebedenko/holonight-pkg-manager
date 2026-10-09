#include "holonight_packages_backends/alpm_update_source.h"

#include "holonight_packages_persistence/alpm_connection_cache.h"
#include "holonight_packages_persistence/catalog_lock.h"
#include "holonight_packages_persistence/json_update_snapshot_store.h"
#include "pacman_config.h"
#include "pending_update_computation.h"
#include "repository_catalog.h"
#include "sync_database_files.h"

#include <QTemporaryDir>

#include <algorithm>
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

std::expected<UpdateSnapshot, UpdateSourceError> selectRepositories(
    const AlpmUpdateSourceOptions& options, const PacmanRepositories& repositories,
    const std::vector<holonight_packages_domain::RepositoryProvenance>& checked, const std::filesystem::path& path) {
  std::error_code error;
  UpdateSnapshot snapshot;
  snapshot.evaluated = true;
  snapshot.databasesFound = false;
  for (const auto& repository : repositories.repositories) {
    auto local =
        describeRepository(repository, repositories, options.databasePath / "sync" / (repository.name + ".db"));
    const auto cached =
        std::ranges::find(checked, repository.name, &holonight_packages_domain::RepositoryProvenance::name);
    std::optional<holonight_packages_domain::RepositoryProvenance> selected = local;
    if (cached != checked.end()) {
      auto validated = describeRepository(repository, repositories, cached->database);
      if (validated && validated->identity == cached->identity && validated->digest == cached->digest &&
          (!local || local->digest == cached->digest || local->timestamp <= cached->timestamp)) {
        selected = *cached;
      }
    }
    if (!selected) {
      continue;
    }
    if (!std::filesystem::copy_file(selected->database, path / "sync" / (repository.name + ".db"), error) || error) {
      return std::unexpected(databaseError("Repository changed during offline evaluation"));
    }
    const auto copied = describeRepository(repository, repositories, path / "sync" / (repository.name + ".db"));
    if (!copied || copied->digest != selected->digest) {
      return std::unexpected(databaseError("Repository changed during offline evaluation"));
    }
    if (!snapshot.databasesFound || selected->timestamp < snapshot.dataAsOf) {
      snapshot.dataAsOf = selected->timestamp;
    }
    snapshot.databasesFound = true;
    snapshot.repositories.push_back(*selected);
  }
  if (!snapshot.databasesFound) {
    return std::unexpected(databaseError("No usable repository data"));
  }
  if (snapshot.repositories.size() != repositories.repositories.size()) {
    return std::unexpected(databaseError("A configured repository is unavailable"));
  }
  return snapshot;
}

}  // namespace

AlpmUpdateSource::AlpmUpdateSource(AlpmUpdateSourceOptions options)
    : options_(std::move(options)),
      connection_cache_(std::make_unique<holonight_packages_persistence::AlpmConnectionCache>(options_.databaseRoot,
                                                                                              options_.databasePath)) {}

AlpmUpdateSource::~AlpmUpdateSource() = default;

std::vector<std::filesystem::path> AlpmUpdateSource::watchPaths() const {
  std::vector<std::filesystem::path> paths{
      options_.databasePath / "local",
      options_.databasePath / "sync",
      options_.pacmanConfPath,
      options_.snapshotFile,
  };
  {
    const std::scoped_lock lock(watch_mutex_);
    if (const auto repositories = parsePacmanRepositories(options_.pacmanConfPath, machineArchitecture());
        repositories) {
      configuration_watch_paths_ = repositories->configuration_files;
    }
    // Preserve missing Include paths and their ancestors so recreation can recover without Reload.
    paths.insert(paths.end(), configuration_watch_paths_.begin(), configuration_watch_paths_.end());
  }
  if (!options_.snapshotFile.empty()) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(options_.snapshotFile.parent_path(), error)) {
      const auto name = entry.path().filename().string();
      if (name.starts_with("catalog-") || name.starts_with("provenance-")) {
        paths.push_back(entry.path());
      }
    }
  }
  return paths;
}

std::expected<UpdateSnapshot, UpdateSourceError> AlpmUpdateSource::loadLocalUpdates() const {
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
      .updates = {},
      .databasesFound = true,
      .dataAsOf = std::chrono::clock_cast<std::chrono::system_clock>(**oldest),
  };
  snapshot.updates = computePendingUpdates(pkgcache, connection->syncDatabases(), *config);
  return snapshot;
}

std::expected<UpdateSnapshot, UpdateSourceError> AlpmUpdateSource::loadUpdates() const {
  if (options_.snapshotFile.empty()) {
    return loadLocalUpdates();
  }

  const auto config = parsePacmanConfig(options_.pacmanConfPath);
  if (!config) {
    return std::unexpected(
        UpdateSourceError{.code = UpdateSourceErrorCode::ConfigurationInvalid, .message = config.error()});
  }
  const holonight_packages_persistence::CatalogLock lease(options_.snapshotFile);
  std::vector<holonight_packages_domain::RepositoryProvenance> checked;
  const auto saved = holonight_packages_persistence::JsonUpdateSnapshotStore(options_.snapshotFile).load();
  if (saved && saved->snapshot && saved->snapshot->snapshot.repositories.empty()) {
    auto previous = saved->snapshot->snapshot;
    previous.previously_loaded = true;
    return previous;
  }
  if (lease.valid() && saved && saved->snapshot) {
    checked = saved->snapshot->snapshot.repositories;
  }
  const auto repositories = parsePacmanRepositories(options_.pacmanConfPath, machineArchitecture());
  if (!repositories) {
    // Local enumeration has never required usable mirrorlists. Only checked-catalog reuse requires their identity.
    if (checked.empty()) {
      auto local = loadLocalUpdates();
      if (local) {
        local->evaluated = true;
      }
      return local;
    }
    return std::unexpected(
        UpdateSourceError{.code = UpdateSourceErrorCode::ConfigurationInvalid, .message = repositories.error()});
  }
  if (const auto local = checkLocalDatabase(options_.databasePath); !local) {
    return std::unexpected(databaseError(local.error()));
  }
  // A private layout gives libalpm the selected repositories in configured order, without touching system data.
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    return std::unexpected(databaseError("Cannot create offline evaluation directory"));
  }
  const std::filesystem::path path(temporary.path().toStdString());
  std::error_code error;
  std::filesystem::create_directory(path / "sync", error);
  std::filesystem::create_directory_symlink(options_.databasePath / "local", path / "local", error);
  if (error) {
    return std::unexpected(databaseError("Cannot prepare offline evaluation"));
  }
  auto selection = selectRepositories(options_, *repositories, checked, path);
  if (!selection) {
    return std::unexpected(selection.error());
  }
  auto snapshot = std::move(*selection);
  if (!snapshot.databasesFound) {
    return snapshot;
  }
  alpm_errno_t init_error = ALPM_ERR_OK;
  const std::unique_ptr<alpm_handle_t, holonight_packages_persistence::detail::AlpmHandleDeleter> handle(
      alpm_initialize(options_.databaseRoot.c_str(), path.c_str(), &init_error));
  if (!handle) {
    return std::unexpected(databaseError(alpm_strerror(init_error)));
  }
  std::vector<alpm_db_t*> databases;
  for (const auto& repository : snapshot.repositories) {
    auto* database = alpm_register_syncdb(handle.get(), repository.name.c_str(), 0);
    if (database == nullptr || alpm_db_get_valid(database) != 0) {
      return std::unexpected(databaseError("Cannot read selected repository"));
    }
    databases.push_back(database);
  }
  auto* installed = alpm_db_get_pkgcache(alpm_get_localdb(handle.get()));
  if (installed == nullptr && alpm_errno(handle.get()) != ALPM_ERR_OK) {
    return std::unexpected(databaseError("Cannot read installed packages"));
  }
  snapshot.updates = computePendingUpdates(installed, databases, *config);
  return snapshot;
}

}  // namespace holonight_packages_backends
