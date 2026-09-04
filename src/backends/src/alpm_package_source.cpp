#include "holonight_packages_backends/alpm_package_source.h"

#include "alpm_package_conversion.h"

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
  const std::filesystem::directory_iterator end;
  for (auto it = std::filesystem::directory_iterator(sync_dir, iterate_error); !iterate_error && it != end;
       it.increment(iterate_error)) {
    if (it->path().extension() != ".db") {
      continue;
    }
    repository_names.push_back(it->path().stem().string());
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

std::expected<Package, PackageSourceError> toPackage(alpm_pkg_t* pkg, const std::vector<alpm_db_t*>& sync_dbs) {
  const char* name = alpm_pkg_get_name(pkg);
  for (alpm_db_t* sync_db : sync_dbs) {
    if (name != nullptr && alpm_db_get_pkg(sync_db, name) != nullptr) {
      return detail::convertPackageFields(name, alpm_pkg_get_version(pkg), alpm_db_get_name(sync_db),
                                          SourceType::Official, toInstallReason(alpm_pkg_get_reason(pkg)));
    }
  }
  return detail::convertPackageFields(name, alpm_pkg_get_version(pkg), "", SourceType::Foreign,
                                      toInstallReason(alpm_pkg_get_reason(pkg)));
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
    auto package = toPackage(static_cast<alpm_pkg_t*>(node->data), *sync_dbs);
    if (!package.has_value()) {
      return std::unexpected(std::move(package.error()));
    }
    packages.push_back(std::move(*package));
  }
  return packages;
}

}  // namespace holonight_packages_backends
