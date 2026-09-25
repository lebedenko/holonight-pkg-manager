#include "sync_database_files.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace holonight_packages_backends {

std::expected<std::optional<std::filesystem::file_time_type>, std::string> oldestSyncDatabaseTime(
    const std::filesystem::path& database_path) {
  const auto sync_dir = database_path / "sync";
  std::error_code status_error;
  const auto status = std::filesystem::status(sync_dir, status_error);
  if (status_error && status.type() != std::filesystem::file_type::not_found) {
    return std::unexpected("Failed to inspect the sync database directory: " + status_error.message());
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
      return std::unexpected("Failed to read the modification time of " + it->path().string() + ": " +
                             time_error.message());
    }
    if (!oldest.has_value() || modified < *oldest) {
      oldest = modified;
    }
  }
  if (iterate_error) {
    return std::unexpected("Failed to enumerate sync databases: " + iterate_error.message());
  }
  return oldest;
}

std::expected<void, std::string> checkLocalDatabase(const std::filesystem::path& database_path) {
  const auto local_dir = database_path / "local";
  std::error_code local_error;
  if (!std::filesystem::is_directory(local_dir, local_error)) {
    return std::unexpected("Cannot read local package database directory " + local_dir.string() + ": " +
                           (local_error ? local_error.message() : "not a directory"));
  }
  const auto version_file = local_dir / "ALPM_DB_VERSION";
  if (!std::filesystem::is_regular_file(version_file, local_error)) {
    return std::unexpected("Cannot read local package database version file " + version_file.string() + ": " +
                           (local_error ? local_error.message() : "not a regular file"));
  }
  return {};
}

}  // namespace holonight_packages_backends
