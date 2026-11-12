#include "guard_snapshot.h"

#include <algorithm>
#include <alpm.h>
#include <cstdlib>
#include <memory>
#include <sys/stat.h>
#include <system_error>

namespace holonight_packages_transaction {

using holonight_packages_domain::SyncFileStamp;

std::optional<std::string> sha256Of(const std::filesystem::path& file) {
  const std::unique_ptr<char, decltype(&std::free)> digest(alpm_compute_sha256sum(file.c_str()), &std::free);
  if (digest == nullptr) {
    return std::nullopt;
  }
  return std::string(digest.get());
}

std::expected<std::vector<SyncFileStamp>, std::string> snapshotSyncFiles(const std::filesystem::path& syncDirectory) {
  std::vector<SyncFileStamp> stamps;
  std::error_code error;
  if (!std::filesystem::is_directory(syncDirectory, error)) {
    return stamps;
  }
  for (const auto& entry : std::filesystem::directory_iterator(syncDirectory, error)) {
    const std::string name = entry.path().filename().string();
    if (!name.ends_with(".db") && !name.ends_with(".db.sig")) {
      continue;
    }
    struct stat status{};
    if (::lstat(entry.path().c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
      return std::unexpected("cannot stat " + entry.path().string());
    }
    auto digest = sha256Of(entry.path());
    if (!digest) {
      return std::unexpected("cannot hash " + entry.path().string());
    }
    stamps.push_back(SyncFileStamp{
        .name = name,
        .sha256 = std::move(*digest),
        .size = static_cast<std::uint64_t>(status.st_size),
        .mtime_ns = (static_cast<std::int64_t>(status.st_mtim.tv_sec) * 1'000'000'000) + status.st_mtim.tv_nsec,
    });
  }
  if (error) {
    return std::unexpected("cannot list " + syncDirectory.string());
  }
  std::ranges::sort(stamps, {}, &SyncFileStamp::name);
  return stamps;
}

}  // namespace holonight_packages_transaction
