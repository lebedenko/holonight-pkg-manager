#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace holonight_packages_backends {

// Returns the mtime of the oldest {database_path}/sync/*.db, or nullopt when the directory is missing, is not a
// directory, or holds no databases. Those cases are the "no databases" state rather than errors; real I/O failures
// (for example permission denied) are errors carrying a user-presentable message.
[[nodiscard]] std::expected<std::optional<std::filesystem::file_time_type>, std::string> oldestSyncDatabaseTime(
    const std::filesystem::path& database_path);

// alpm_initialize can create local/ and its version marker. Callers check first, so a missing local database is an
// error rather than an empty result. The error is a user-presentable message.
[[nodiscard]] std::expected<void, std::string> checkLocalDatabase(const std::filesystem::path& database_path);

}  // namespace holonight_packages_backends
