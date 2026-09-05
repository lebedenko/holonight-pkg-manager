#include "holonight_packages_backends/alpm_package_source.h"

#include "alpm_package_conversion.h"
#include "holonight_packages_persistence/alpm_connection_cache.h"

#include <alpm.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace holonight_packages_backends {

namespace {

using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;
using holonight_packages_domain::PackageSourceError;
using holonight_packages_domain::PackageSourceErrorCode;
using holonight_packages_domain::SourceType;

InstallReason toInstallReason(alpm_pkgreason_t reason) {
  return reason == ALPM_PKG_REASON_EXPLICIT ? InstallReason::Explicit : InstallReason::Dependency;
}

// Fills the 6 fields libalpm's toPackage() base conversion doesn't cover, from the same alpm_pkg_t*
// (one enumeration pass, no extra libalpm round-trips per REQ-F-102).
void populateExtendedFields(Package& package, alpm_pkg_t* pkg) {
  const off_t installed_size = alpm_pkg_get_isize(pkg);
  package.sizeBytes = installed_size > 0 ? static_cast<std::uint64_t>(installed_size) : 0;

  const char* description = alpm_pkg_get_desc(pkg);
  package.description = description != nullptr ? description : "";

  package.installDate = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(alpm_pkg_get_installdate(pkg)));

  // alpm_pkg_compute_requiredby ALLOCATES a new list of newly-strdup'd strings; unlike the borrowed
  // accessors below, caller must free both the strings and the list.
  alpm_list_t* required_by = alpm_pkg_compute_requiredby(pkg);
  for (alpm_list_t* node = required_by; node != nullptr; node = alpm_list_next(node)) {
    package.requiredBy.emplace_back(static_cast<const char*>(node->data));
  }
  alpm_list_free_inner(required_by, free);
  alpm_list_free(required_by);

  // alpm_pkg_get_optdepends returns the package's own cached list; do not free the list or its
  // alpm_depend_t* entries, only the string alpm_dep_compute_string() allocates needs freeing.
  for (alpm_list_t* node = alpm_pkg_get_optdepends(pkg); node != nullptr; node = alpm_list_next(node)) {
    auto* dependency = static_cast<alpm_depend_t*>(node->data);
    std::unique_ptr<char, decltype(&free)> dependency_string(alpm_dep_compute_string(dependency), &free);
    if (dependency_string != nullptr) {
      package.optionalDependencies.emplace_back(dependency_string.get());
    }
  }

  // alpm_pkg_get_backup is also a borrowed cached list; just count it.
  package.configFileCount = static_cast<std::size_t>(alpm_list_count(alpm_pkg_get_backup(pkg)));
}

std::expected<Package, PackageSourceError> toPackage(alpm_pkg_t* pkg, const std::vector<alpm_db_t*>& sync_dbs) {
  const char* name = alpm_pkg_get_name(pkg);
  auto package = [&]() -> std::expected<Package, PackageSourceError> {
    for (alpm_db_t* sync_db : sync_dbs) {
      if (name != nullptr && alpm_db_get_pkg(sync_db, name) != nullptr) {
        return detail::convertPackageFields(name, alpm_pkg_get_version(pkg), alpm_db_get_name(sync_db),
                                            SourceType::Official, toInstallReason(alpm_pkg_get_reason(pkg)));
      }
    }
    return detail::convertPackageFields(name, alpm_pkg_get_version(pkg), "", SourceType::Foreign,
                                        toInstallReason(alpm_pkg_get_reason(pkg)));
  }();
  if (package.has_value()) {
    populateExtendedFields(*package, pkg);
  }
  return package;
}

}  // namespace

std::expected<Package, PackageSourceError> detail::convertPackageFields(const char* name, const char* version,
                                                                        const char* repository, SourceType source_type,
                                                                        InstallReason install_reason) {
  const auto missingField = [](const char* field) {
    return std::unexpected(PackageSourceError{.code = PackageSourceErrorCode::Unknown,
                                              .message = std::string("libalpm returned a null ") + field});
  };
  if (name == nullptr) {
    return missingField("package name");
  }
  if (version == nullptr) {
    return missingField("package version");
  }
  if (repository == nullptr) {
    return missingField("repository name");
  }

  return Package{.identity = name,
                 .name = name,
                 .installedVersion = version,
                 .sourceType = source_type,
                 .repository = repository,
                 .installReason = install_reason,
                 .backendSpecificId = name};
}

AlpmPackageSource::AlpmPackageSource(std::filesystem::path database_root, std::filesystem::path database_path)
    : database_root_(std::move(database_root)),
      database_path_(std::move(database_path)),
      connection_cache_(
          std::make_unique<holonight_packages_persistence::AlpmConnectionCache>(database_root_, database_path_)) {}

AlpmPackageSource::~AlpmPackageSource() = default;

std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>
AlpmPackageSource::enumerateInstalledPackages() const {
  auto connection = connection_cache_->connection();
  if (!connection.has_value()) {
    return std::unexpected(std::move(connection.error()));
  }

  alpm_errno_t init_error = ALPM_ERR_OK;
  std::unique_ptr<alpm_handle_t, holonight_packages_persistence::detail::AlpmHandleDeleter> local_handle(
      alpm_initialize(database_root_.c_str(), database_path_.c_str(), &init_error));
  if (!local_handle) {
    const auto code = init_error == ALPM_ERR_NOT_A_DIR ? PackageSourceErrorCode::DatabaseRootInvalid
                                                       : PackageSourceErrorCode::DatabaseOpenFailed;
    return std::unexpected(PackageSourceError{.code = code, .message = alpm_strerror(init_error)});
  }

  alpm_db_t* local_db = alpm_get_localdb(local_handle.get());
  alpm_list_t* pkgcache = alpm_db_get_pkgcache(local_db);
  if (pkgcache == nullptr && alpm_errno(local_handle.get()) != ALPM_ERR_OK) {
    return std::unexpected(PackageSourceError{
        .code = PackageSourceErrorCode::DatabaseOpenFailed,
        .message = alpm_strerror(alpm_errno(local_handle.get())),
    });
  }

  std::vector<Package> packages;
  for (alpm_list_t* node = pkgcache; node != nullptr; node = alpm_list_next(node)) {
    auto package = toPackage(static_cast<alpm_pkg_t*>(node->data), connection->syncDatabases());
    if (!package.has_value()) {
      return std::unexpected(std::move(package.error()));
    }
    packages.push_back(std::move(*package));
  }
  return packages;
}

}  // namespace holonight_packages_backends
