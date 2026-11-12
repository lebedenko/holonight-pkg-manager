#include "scratch_dbpath.h"

#include "guard_snapshot.h"

#include <sys/stat.h>
#include <system_error>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

TransactionFailure refuse(TransactionErrorCode code) {
  return TransactionFailure{.code = code, .names = {}, .choices = {}};
}

constexpr fs::perms kTraverseOnly = fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec;

bool makeDirectory(const fs::path& path) {
  std::error_code error;
  fs::create_directories(path, error);
  if (error) {
    return false;
  }
  fs::permissions(path, kTraverseOnly, fs::perm_options::replace, error);
  return !error;
}

// Copies the files of the configured repositories. Returns the bytes copied.
std::expected<std::uint64_t, std::string> seed(const fs::path& liveSync, const fs::path& scratchSync,
                                               const std::vector<std::string>& repositories) {
  std::uint64_t bytes = 0;
  for (const std::string& repository : repositories) {
    for (const std::string& name : {repository + ".db", repository + ".db.sig"}) {
      const fs::path source = liveSync / name;
      std::error_code error;
      if (!fs::is_regular_file(source, error)) {
        continue;
      }
      const fs::path target = scratchSync / name;
      fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
      if (error) {
        return std::unexpected("cannot copy " + source.string());
      }
      fs::last_write_time(target, fs::last_write_time(source), error);
      if (error) {
        return std::unexpected("cannot set the time of " + target.string());
      }
      bytes += fs::file_size(target);
    }
  }
  return bytes;
}

}  // namespace

std::expected<SeededScratch, TransactionFailure> prepareScratchDbpath(const fs::path& live_dbpath,
                                                                      const ScratchDbpath& scratch,
                                                                      const std::vector<std::string>& repositories,
                                                                      const LogSink& journal) {
  const auto log = [&journal](const std::string& text) {
    if (journal) {
      journal(LogLevel::Error, text);
    }
  };
  std::error_code error;
  if (fs::exists(live_dbpath / "db.lck", error)) {
    return std::unexpected(refuse(TransactionErrorCode::Busy));  // another package manager runs
  }
  if (!makeDirectory(scratch.sync()) || !makeDirectory(scratch.rollback())) {
    log("cannot create the scratch layout");
    return std::unexpected(refuse(TransactionErrorCode::InternalError));
  }
  ::chmod(scratch.root.c_str(), 0711);
  ::chmod(scratch.dbpath().c_str(), 0711);
  fs::create_directory_symlink(live_dbpath / "local", scratch.dbpath() / "local", error);
  if (error) {
    log("cannot link the local database into the scratch dbpath");
    return std::unexpected(refuse(TransactionErrorCode::InternalError));
  }
  const fs::path liveSync = live_dbpath / "sync";
  for (int attempt = 0; attempt < 2; ++attempt) {
    auto before = snapshotSyncFiles(liveSync);
    if (!before) {
      log(before.error());
      return std::unexpected(refuse(TransactionErrorCode::DatabaseUnavailable));
    }
    auto copied = seed(liveSync, scratch.sync(), repositories);
    if (!copied) {
      log(copied.error());
      return std::unexpected(refuse(TransactionErrorCode::InternalError));
    }
    auto after = snapshotSyncFiles(liveSync);
    if (after && *after == *before) {
      return SeededScratch{.guard = std::move(*before), .seeded_bytes = *copied};
    }
  }
  log("the live sync databases changed twice while they were copied");
  return std::unexpected(refuse(TransactionErrorCode::SyncFailed));
}

}  // namespace holonight_packages_transaction
