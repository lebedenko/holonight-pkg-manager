#pragma once

#include "holonight_packages_domain/upgrade_plan.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

// The SHA-256 of a file as lowercase hex, or nothing when it cannot be read.
[[nodiscard]] std::optional<std::string> sha256Of(const std::filesystem::path& file);

// Name, SHA-256, size and modification time of every `*.db` and `*.db.sig` file directly in `syncDirectory`, sorted by
// name (system-upgrade REQ-F-051). Other files there, such as `*.files` from `pacman -Fy`, are outside the guard. A
// missing directory gives an empty list; an unreadable file is an error.
[[nodiscard]] std::expected<std::vector<holonight_packages_domain::SyncFileStamp>, std::string> snapshotSyncFiles(
    const std::filesystem::path& syncDirectory);

}  // namespace holonight_packages_transaction
