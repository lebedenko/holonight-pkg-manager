#include "holonight_packages_backends/alpm_explore_source.h"

#include "holonight_packages_persistence/alpm_connection_cache.h"
#include "sync_database_files.h"

#include <alpm.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace holonight_packages_backends {

namespace {

using holonight_packages_domain::ExploreSnapshot;
using holonight_packages_domain::ExploreSourceError;
using holonight_packages_domain::ExploreSourceErrorCode;
using holonight_packages_domain::SyncPackage;

ExploreSourceError databaseError(std::string message) {
  return ExploreSourceError{.code = ExploreSourceErrorCode::DatabaseOpenFailed, .message = std::move(message)};
}

std::string stringOrEmpty(const char* text) { return text != nullptr ? text : ""; }

std::uint64_t nonNegative(off_t value) { return value > 0 ? static_cast<std::uint64_t>(value) : 0; }

// Borrowed list of C strings (licenses); neither the list nor the strings are freed.
std::vector<std::string> stringList(alpm_list_t* list) {
  std::vector<std::string> out;
  for (alpm_list_t* node = list; node != nullptr; node = alpm_list_next(node)) {
    out.emplace_back(static_cast<const char*>(node->data));
  }
  return out;
}

// Borrowed list of alpm_depend_t*; only the string alpm_dep_compute_string allocates is freed.
std::vector<std::string> dependencyList(alpm_list_t* list) {
  std::vector<std::string> out;
  for (alpm_list_t* node = list; node != nullptr; node = alpm_list_next(node)) {
    const std::unique_ptr<char, decltype(&free)> text(alpm_dep_compute_string(static_cast<alpm_depend_t*>(node->data)),
                                                      &free);
    if (text != nullptr) {
      out.emplace_back(text.get());
    }
  }
  return out;
}

std::expected<SyncPackage, ExploreSourceError> convert(alpm_pkg_t* pkg, const char* repository,
                                                       const std::unordered_map<std::string, std::string>& installed) {
  const char* name = alpm_pkg_get_name(pkg);
  const char* version = alpm_pkg_get_version(pkg);
  if (name == nullptr || version == nullptr || repository == nullptr) {
    return std::unexpected(databaseError("libalpm returned a package without a name, version or repository"));
  }
  SyncPackage package{
      .name = name,
      .version = version,
      .repository = repository,
      .description = stringOrEmpty(alpm_pkg_get_desc(pkg)),
      .url = stringOrEmpty(alpm_pkg_get_url(pkg)),
      .licenses = stringList(alpm_pkg_get_licenses(pkg)),
      .dependencies = dependencyList(alpm_pkg_get_depends(pkg)),
      .optionalDependencies = dependencyList(alpm_pkg_get_optdepends(pkg)),
      .downloadSizeBytes = nonNegative(alpm_pkg_get_size(pkg)),
      .installedSizeBytes = nonNegative(alpm_pkg_get_isize(pkg)),
      .installedVersion = std::nullopt,
  };
  if (const auto found = installed.find(package.name); found != installed.end()) {
    package.installedVersion = found->second;
  }
  return package;
}

}  // namespace

AlpmExploreSource::AlpmExploreSource(AlpmExploreSourceOptions options)
    : options_(std::move(options)),
      connection_cache_(std::make_unique<holonight_packages_persistence::AlpmConnectionCache>(options_.databaseRoot,
                                                                                              options_.databasePath)) {}

AlpmExploreSource::~AlpmExploreSource() = default;

std::expected<ExploreSnapshot, ExploreSourceError> AlpmExploreSource::loadPackages() const {
  const auto oldest = oldestSyncDatabaseTime(options_.databasePath);
  if (!oldest.has_value()) {
    return std::unexpected(databaseError(oldest.error()));
  }
  if (!oldest->has_value()) {
    return ExploreSnapshot{.packages = {}, .databasesFound = false, .dataAsOf = {}};
  }

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
  alpm_list_t* local_cache = alpm_db_get_pkgcache(alpm_get_localdb(local_handle.get()));
  if (local_cache == nullptr && alpm_errno(local_handle.get()) != ALPM_ERR_OK) {
    return std::unexpected(databaseError(alpm_strerror(alpm_errno(local_handle.get()))));
  }
  std::unordered_map<std::string, std::string> installed;
  for (alpm_list_t* node = local_cache; node != nullptr; node = alpm_list_next(node)) {
    auto* pkg = static_cast<alpm_pkg_t*>(node->data);
    const char* name = alpm_pkg_get_name(pkg);
    const char* version = alpm_pkg_get_version(pkg);
    if (name != nullptr && version != nullptr) {
      installed.emplace(name, version);
    }
  }

  ExploreSnapshot snapshot{
      .packages = {}, .databasesFound = true, .dataAsOf = std::chrono::clock_cast<std::chrono::system_clock>(**oldest)};
  for (alpm_db_t* sync_db : connection->syncDatabases()) {
    alpm_list_t* cache = alpm_db_get_pkgcache(sync_db);
    if (cache == nullptr && alpm_errno(connection->handle()) != ALPM_ERR_OK) {
      return std::unexpected(databaseError(alpm_strerror(alpm_errno(connection->handle()))));
    }
    const char* repository = alpm_db_get_name(sync_db);
    for (alpm_list_t* node = cache; node != nullptr; node = alpm_list_next(node)) {
      auto package = convert(static_cast<alpm_pkg_t*>(node->data), repository, installed);
      if (!package.has_value()) {
        return std::unexpected(std::move(package.error()));
      }
      snapshot.packages.push_back(std::move(*package));
    }
  }
  return snapshot;
}

}  // namespace holonight_packages_backends
