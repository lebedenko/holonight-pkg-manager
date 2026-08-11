#include "holonight_packages_backends/alpm_package_source.h"

#include <algorithm>
#include <alpm.h>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace holonight_packages_backends {

namespace {

using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;
using holonight_packages_domain::PackageSourceError;
using holonight_packages_domain::PackageSourceErrorCode;
using holonight_packages_domain::SourceType;

struct AlpmHandleDeleter {
  void operator()(alpm_handle_t* handle) const {
    if (handle != nullptr) {
      alpm_release(handle);
    }
  }
};
using AlpmHandlePtr = std::unique_ptr<alpm_handle_t, AlpmHandleDeleter>;

InstallReason toInstallReason(alpm_pkgreason_t reason) {
  return reason == ALPM_PKG_REASON_EXPLICIT ? InstallReason::Explicit : InstallReason::Dependency;
}

using SyncDatabasesResult = std::expected<std::vector<alpm_db_t*>, PackageSourceError>;

PackageSourceError databaseError(std::string message) {
  return PackageSourceError{.code = PackageSourceErrorCode::DatabaseOpenFailed, .message = std::move(message)};
}

SyncDatabasesResult registerSyncDatabases(alpm_handle_t* handle, const std::filesystem::path& database_path) {
  std::vector<alpm_db_t*> sync_dbs;
  const auto sync_dir = database_path / "sync";
  std::error_code exists_error;
  const bool sync_dir_exists = std::filesystem::exists(sync_dir, exists_error);
  if (exists_error) {
    return std::unexpected(databaseError("Failed to inspect sync database directory: " + exists_error.message()));
  }
  if (!sync_dir_exists) {
    return sync_dbs;
  }

  std::error_code type_error;
  if (!std::filesystem::is_directory(sync_dir, type_error) || type_error) {
    const std::string detail = type_error ? type_error.message() : "path is not a directory";
    return std::unexpected(databaseError("Invalid sync database directory: " + detail));
  }

  std::vector<std::string> repository_names;
  std::error_code iterate_error;
  for (const auto& entry : std::filesystem::directory_iterator(sync_dir, iterate_error)) {
    if (entry.path().extension() != ".db") {
      continue;
    }
    repository_names.push_back(entry.path().stem().string());
  }
  if (iterate_error) {
    return std::unexpected(databaseError("Failed to enumerate sync databases: " + iterate_error.message()));
  }

  std::ranges::sort(repository_names);
  for (const std::string& repo_name : repository_names) {
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

Package toPackage(alpm_pkg_t* pkg, const std::vector<alpm_db_t*>& sync_dbs) {
  const char* name = alpm_pkg_get_name(pkg);

  Package package;
  package.identity = name;
  package.name = name;
  package.installedVersion = alpm_pkg_get_version(pkg);
  package.installReason = toInstallReason(alpm_pkg_get_reason(pkg));
  package.backendSpecificId = name;

  for (alpm_db_t* sync_db : sync_dbs) {
    if (alpm_db_get_pkg(sync_db, name) != nullptr) {
      package.sourceType = SourceType::Official;
      package.repository = alpm_db_get_name(sync_db);
      return package;
    }
  }
  package.sourceType = SourceType::Foreign;
  return package;
}

}  // namespace

AlpmPackageSource::AlpmPackageSource(std::filesystem::path database_root, std::filesystem::path database_path)
    : database_root_(std::move(database_root)), database_path_(std::move(database_path)) {}

std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>
AlpmPackageSource::enumerateInstalledPackages() const {
  alpm_errno_t init_error = ALPM_ERR_OK;
  AlpmHandlePtr handle(alpm_initialize(database_root_.c_str(), database_path_.c_str(), &init_error));
  if (!handle) {
    const PackageSourceErrorCode code = init_error == ALPM_ERR_NOT_A_DIR ? PackageSourceErrorCode::DatabaseRootInvalid
                                                                         : PackageSourceErrorCode::DatabaseOpenFailed;
    return std::unexpected(PackageSourceError{.code = code, .message = alpm_strerror(init_error)});
  }

  auto sync_dbs = registerSyncDatabases(handle.get(), database_path_);
  if (!sync_dbs.has_value()) {
    return std::unexpected(std::move(sync_dbs.error()));
  }

  alpm_db_t* local_db = alpm_get_localdb(handle.get());
  alpm_list_t* pkgcache = alpm_db_get_pkgcache(local_db);
  if (pkgcache == nullptr && alpm_errno(handle.get()) != ALPM_ERR_OK) {
    return std::unexpected(PackageSourceError{
        .code = PackageSourceErrorCode::DatabaseOpenFailed,
        .message = alpm_strerror(alpm_errno(handle.get())),
    });
  }

  std::vector<Package> packages;
  for (alpm_list_t* node = pkgcache; node != nullptr; node = alpm_list_next(node)) {
    packages.push_back(toPackage(static_cast<alpm_pkg_t*>(node->data), *sync_dbs));
  }
  return packages;
}

}  // namespace holonight_packages_backends
