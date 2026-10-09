#include "holonight_packages_backends/alpm_update_checker.h"

#include "alpm_error_mapping.h"
#include "pacman_config.h"
#include "pacman_repositories.h"
#include "pending_update_computation.h"
#include "scratch_dir.h"
#include "sync_database_files.h"

#include <QDebug>

#include <algorithm>
#include <alpm.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace holonight_packages_backends {

namespace {

using holonight_packages_domain::UpdateCheckError;
using holonight_packages_domain::UpdateCheckErrorCode;
using holonight_packages_domain::UpdateSnapshot;

using CheckResult = std::expected<UpdateSnapshot, UpdateCheckError>;

constexpr std::string_view kFileScheme = "file://";

std::unexpected<UpdateCheckError> fail(UpdateCheckErrorCode code, const char* why) {
  // Only fixed, credential-free text is logged: never a server URL.
  qWarning() << "update check failed:" << why;
  return std::unexpected(UpdateCheckError{code});
}

struct SourceStamp {
  std::filesystem::path path;
  off_t size = 0;
  struct timespec modified{};
};

bool sameStamp(const SourceStamp& before, const struct stat& now) {
  return before.size == now.st_size && before.modified.tv_sec == now.st_mtim.tv_sec &&
         before.modified.tv_nsec == now.st_mtim.tv_nsec;
}

struct Descriptor {
  int fd = -1;
  explicit Descriptor(int value) : fd(value) {}
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;
  ~Descriptor() {
    if (fd >= 0) {
      ::close(fd);
    }
  }
};

bool isDatabaseFile(const std::string& name) { return name.ends_with(".db") || name.ends_with(".db.sig"); }

// Writes the whole chunk, retrying on EINTR and short writes.
bool writeAll(int descriptor, std::span<const char> chunk) {
  while (!chunk.empty()) {
    const ssize_t result = ::write(descriptor, chunk.data(), chunk.size());
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    chunk = chunk.subspan(static_cast<std::size_t>(result));
  }
  return true;
}

// Copies one regular file without following symlinks and gives the copy the source's mtime, which libalpm sends as
// If-Modified-Since. Records the source's (size, mtime) for the post-copy comparison.
bool copyPreservingMtime(const std::filesystem::path& source, const std::filesystem::path& destination,
                         SourceStamp& stamp) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
  const Descriptor input(::open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  if (input.fd < 0) {
    return false;
  }
  struct stat info{};
  if (::fstat(input.fd, &info) != 0 || !S_ISREG(info.st_mode)) {
    return false;
  }
  stamp = SourceStamp{.path = source, .size = info.st_size, .modified = info.st_mtim};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
  const Descriptor output(::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
  if (output.fd < 0) {
    return false;
  }
  std::vector<char> buffer(64 * 1024);
  while (true) {
    const ssize_t count = ::read(input.fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      return false;
    }
    if (count == 0) {
      break;
    }
    if (!writeAll(output.fd, std::span<const char>(buffer).first(static_cast<std::size_t>(count)))) {
      return false;
    }
  }
  const std::array<timespec, 2> times{info.st_mtim, info.st_mtim};
  return ::futimens(output.fd, times.data()) == 0;
}

std::filesystem::path localPath(const std::string& url) { return {url.substr(kFileScheme.size())}; }

bool isFileUrl(const std::string& url) { return url.starts_with(kFileScheme); }

// Removes the other run directories on every exit once the run directory exists.
struct RunDirectoryGuard {
  RunDirectoryGuard(std::filesystem::path scratch_root, std::filesystem::path run_directory)
      : root(std::move(scratch_root)), run(std::move(run_directory)) {}
  std::filesystem::path root;
  std::filesystem::path run;
  RunDirectoryGuard(const RunDirectoryGuard&) = delete;
  RunDirectoryGuard& operator=(const RunDirectoryGuard&) = delete;
  RunDirectoryGuard(RunDirectoryGuard&&) = delete;
  RunDirectoryGuard& operator=(RunDirectoryGuard&&) = delete;
  ~RunDirectoryGuard() { removeOtherRunDirectories(root, run); }
};

struct HandleDeleter {
  void operator()(alpm_handle_t* handle) const { alpm_release(handle); }
};

// What the preconditions established: the ignore rules, the repositories, and whether any server is remote.
struct CheckPlan {
  PacmanConfig ignore_rules;
  PacmanRepositories repositories;
  ServerKinds kinds = ServerKinds::AllLocal;
};

bool localMirrorDirectoryExists(const PacmanRepository& repository) {
  return std::ranges::any_of(repository.servers, [](const std::string& url) {
    std::error_code directoryError;
    return std::filesystem::is_directory(localPath(url), directoryError);
  });
}

// Steps 4 and 5: the real database is not locked, it is usable, and the configuration is understood.
std::expected<CheckPlan, UpdateCheckError> planCheck(const AlpmUpdateCheckerOptions& options) {
  std::error_code fsError;
  if (std::filesystem::exists(options.databasePath / "db.lck", fsError)) {
    return fail(UpdateCheckErrorCode::Busy, "the package database is locked");
  }
  if (const auto local = checkLocalDatabase(options.databasePath); !local) {
    return fail(UpdateCheckErrorCode::Unknown, "the local package database is not usable");
  }
  auto ignore_rules = parsePacmanConfig(options.pacmanConfPath);
  if (!ignore_rules) {
    return fail(UpdateCheckErrorCode::Unknown, "pacman.conf cannot be read");
  }
  auto repositories = parsePacmanRepositories(options.pacmanConfPath, machineArchitecture());
  if (!repositories || repositories->repositories.empty()) {
    return fail(UpdateCheckErrorCode::Unknown, "pacman.conf has no usable repositories");
  }
  CheckPlan plan{.ignore_rules = std::move(*ignore_rules), .repositories = std::move(*repositories)};
  for (const PacmanRepository& repository : plan.repositories.repositories) {
    if (!std::ranges::all_of(repository.servers, isFileUrl)) {
      plan.kinds = ServerKinds::SomeRemote;
    } else if (!localMirrorDirectoryExists(repository)) {
      return fail(UpdateCheckErrorCode::RepositoryUnreachable, "a local repository directory is missing");
    }
  }
  return plan;
}

// Step 7: local is a symlink to the real local database; sync is a copy with preserved mtimes. Returns the stamps of
// the copied sources.
std::expected<std::vector<SourceStamp>, UpdateCheckError> layOutScratch(const AlpmUpdateCheckerOptions& options,
                                                                        const std::filesystem::path& run) {
  if (::symlink((options.databasePath / "local").c_str(), (run / "local").c_str()) != 0) {
    return fail(UpdateCheckErrorCode::Unknown, "cannot link the local database");
  }
  if (::mkdir((run / "sync").c_str(), 0700) != 0) {
    return fail(UpdateCheckErrorCode::Unknown, "cannot create the scratch sync directory");
  }
  std::vector<SourceStamp> stamps;
  std::error_code fsError;
  const std::filesystem::path realSync = options.databasePath / "sync";
  if (!std::filesystem::is_directory(realSync, fsError)) {
    return stamps;
  }
  for (const auto& entry : std::filesystem::directory_iterator(realSync, fsError)) {
    const std::string name = entry.path().filename().string();
    if (!isDatabaseFile(name) || !entry.is_regular_file(fsError)) {
      continue;
    }
    SourceStamp stamp;
    if (!copyPreservingMtime(entry.path(), run / "sync" / name, stamp)) {
      return fail(UpdateCheckErrorCode::Unknown, "cannot copy a sync database");
    }
    stamps.push_back(std::move(stamp));
  }
  return stamps;
}

// Step 8: the real database stayed unlocked and no source changed while it was copied.
std::expected<void, UpdateCheckError> verifyCopy(const AlpmUpdateCheckerOptions& options,
                                                 const std::vector<SourceStamp>& stamps) {
  std::error_code fsError;
  if (std::filesystem::exists(options.databasePath / "db.lck", fsError)) {
    return fail(UpdateCheckErrorCode::Busy, "the package database was locked during the copy");
  }
  for (const SourceStamp& stamp : stamps) {
    struct stat now{};
    if (::stat(stamp.path.c_str(), &now) != 0 || !sameStamp(stamp, now)) {
      return fail(UpdateCheckErrorCode::Busy, "a sync database changed during the copy");
    }
  }
  return {};
}

// Steps 9 and 10: a handle on the scratch dbpath with every repository registered at an explicit, verified level.
std::expected<std::unique_ptr<alpm_handle_t, HandleDeleter>, UpdateCheckError> openHandle(
    const AlpmUpdateCheckerOptions& options, const CheckPlan& plan, const std::filesystem::path& run) {
  alpm_errno_t initError = ALPM_ERR_OK;
  std::unique_ptr<alpm_handle_t, HandleDeleter> handle(
      alpm_initialize(options.databaseRoot.c_str(), run.c_str(), &initError));
  if (!handle) {
    return fail(classifyAlpmError(initError, plan.kinds), "cannot open the scratch database");
  }
  alpm_option_set_logfile(handle.get(), "/dev/null");
  alpm_option_set_gpgdir(handle.get(), plan.repositories.gpg_directory.c_str());
  for (const std::string& architecture : plan.repositories.architectures) {
    alpm_option_add_architecture(handle.get(), architecture.c_str());
  }
  alpm_option_set_default_siglevel(handle.get(), static_cast<int>(plan.repositories.default_sig_level));
  alpm_option_set_disable_dl_timeout(handle.get(), 0);
  alpm_option_set_parallel_downloads(handle.get(), 1);

  for (const PacmanRepository& repository : plan.repositories.repositories) {
    alpm_db_t* database =
        alpm_register_syncdb(handle.get(), repository.name.c_str(), static_cast<int>(repository.sig_level));
    if (database == nullptr) {
      return fail(classifyAlpmError(alpm_errno(handle.get()), plan.kinds), "cannot register a repository");
    }
    if (std::cmp_not_equal(alpm_db_get_siglevel(database), repository.sig_level)) {
      return fail(UpdateCheckErrorCode::Unknown, "the signature level was not applied");
    }
    for (const std::string& server : repository.servers) {
      if (alpm_db_add_server(database, server.c_str()) != 0) {
        return fail(classifyAlpmError(alpm_errno(handle.get()), plan.kinds), "a repository server was rejected");
      }
    }
  }
  return handle;
}

// Steps 11 and 12: refresh the private databases, then compare them with the installed packages.
CheckResult refreshAndCompare(alpm_handle_t* handle, const CheckPlan& plan, const std::filesystem::path& run) {
  // 0 = updated, 1 = already current, -1 = error.
  if (alpm_db_update(handle, alpm_get_syncdbs(handle), 0) < 0) {
    const alpm_errno_t error = alpm_errno(handle);
    qWarning() << "update check failed: refresh error" << static_cast<int>(error) << alpm_strerror(error);
    return std::unexpected(UpdateCheckError{classifyAlpmError(error, plan.kinds)});
  }
  alpm_list_t* pkgcache = alpm_db_get_pkgcache(alpm_get_localdb(handle));
  if (pkgcache == nullptr && alpm_errno(handle) != ALPM_ERR_OK) {
    return fail(UpdateCheckErrorCode::Unknown, "cannot read the local database");
  }
  std::vector<alpm_db_t*> syncDatabases;
  for (alpm_list_t* node = alpm_get_syncdbs(handle); node != nullptr; node = alpm_list_next(node)) {
    syncDatabases.push_back(static_cast<alpm_db_t*>(node->data));
  }
  const auto oldest = oldestSyncDatabaseTime(run);
  if (!oldest || !oldest->has_value()) {
    return fail(UpdateCheckErrorCode::Unknown, "the refreshed databases cannot be read");
  }
  UpdateSnapshot snapshot;
  snapshot.databasesFound = true;
  snapshot.dataAsOf = std::chrono::clock_cast<std::chrono::system_clock>(**oldest);
  snapshot.updates = computePendingUpdates(pkgcache, syncDatabases, plan.ignore_rules);
  return snapshot;
}

CheckResult runCheckBody(const AlpmUpdateCheckerOptions& options) {
  // 1. Scratch root.
  if (const auto prepared = prepareScratchRoot(options.scratchRoot); !prepared) {
    return fail(UpdateCheckErrorCode::Unknown, "the scratch directory is not usable");
  }
  // 2. Exclusion lock, held until this function returns (and so for a wedged worker, until libalpm returns).
  auto lock = ScratchLock::acquire(options.scratchRoot);
  if (!lock) {
    return lock.error().kind == ScratchFailure::Busy
               ? fail(UpdateCheckErrorCode::Busy, "another check is running")
               : fail(UpdateCheckErrorCode::Unknown, "cannot lock the scratch directory");
  }
  // 3. Sweep: nothing else is live under the lock.
  sweepScratchRoot(options.scratchRoot);

  // 4./5. Preconditions.
  const auto plan = planCheck(options);
  if (!plan) {
    return std::unexpected(plan.error());
  }

  // 6. Run directory: always kept (the last one is retained for inspection).
  const auto run = createRunDirectory(options.scratchRoot);
  if (!run) {
    return fail(UpdateCheckErrorCode::Unknown, "cannot create a run directory");
  }
  const RunDirectoryGuard guard{options.scratchRoot, *run};

  // 7./8. Layout and copy verification.
  const auto stamps = layOutScratch(options, *run);
  if (!stamps) {
    return std::unexpected(stamps.error());
  }
  if (options.hooks.afterCopyStarted) {
    options.hooks.afterCopyStarted();
  }
  if (const auto verified = verifyCopy(options, *stamps); !verified) {
    return std::unexpected(verified.error());
  }

  // 9.-12. Open, refresh, compare. The handle is released, then the other run directories are removed, then the lock
  // is released, all by destructors.
  const auto handle = openHandle(options, *plan, *run);
  if (!handle) {
    return std::unexpected(handle.error());
  }
  return refreshAndCompare(handle->get(), *plan, *run);
}

}  // namespace

AlpmUpdateChecker::AlpmUpdateChecker(AlpmUpdateCheckerOptions options)
    : options_(std::make_shared<const AlpmUpdateCheckerOptions>(std::move(options))) {}

AlpmUpdateChecker::~AlpmUpdateChecker() = default;

CheckResult AlpmUpdateChecker::checkForUpdates() const {
  auto promise = std::make_shared<std::promise<CheckResult>>();
  std::future<CheckResult> future = promise->get_future();
  // The worker owns its own copies, so it may outlive this object and this call (libalpm cannot be cancelled).
  std::thread([options = options_, promise] {
    try {
      promise->set_value(runCheckBody(*options));
    } catch (const std::exception&) {
      promise->set_value(std::unexpected(UpdateCheckError{UpdateCheckErrorCode::Unknown}));
    }
    if (options->hooks.onBodyFinished) {
      options->hooks.onBodyFinished();
    }
  }).detach();

  if (future.wait_for(options_->totalBound) == std::future_status::ready) {
    return future.get();
  }
  qWarning() << "update check abandoned after" << options_->totalBound.count() << "seconds";
  return std::unexpected(UpdateCheckError{UpdateCheckErrorCode::NetworkUnavailable});
}

void AlpmUpdateChecker::sweepStale() const {
  if (!prepareScratchRoot(options_->scratchRoot)) {
    return;
  }
  const auto lock = ScratchLock::acquire(options_->scratchRoot);
  if (!lock) {
    return;
  }
  sweepScratchRoot(options_->scratchRoot);
}

}  // namespace holonight_packages_backends
