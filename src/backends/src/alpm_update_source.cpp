#include "holonight_packages_backends/alpm_update_source.h"

#include "holonight_packages_persistence/alpm_connection_cache.h"
#include "pacman_config.h"
#include "pending_update_computation.h"
#include "sync_database_files.h"

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
      .updates = {},
      .databasesFound = true,
      .dataAsOf = std::chrono::clock_cast<std::chrono::system_clock>(**oldest),
  };
  snapshot.updates = computePendingUpdates(pkgcache, connection->syncDatabases(), *config);
  return snapshot;
}

}  // namespace holonight_packages_backends
