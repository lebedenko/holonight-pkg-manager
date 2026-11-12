#include "sync_refresher.h"

#include "failure_reporter.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

TransactionFailure refuse(TransactionErrorCode code) {
  return TransactionFailure{.code = code, .names = {}, .choices = {}};
}

// What the worker and the waiting thread share. Owned jointly, so a stuck worker never touches freed memory.
struct SyncState {
  std::mutex mutex;
  std::condition_variable changed;
  bool done = false;
  bool cap_exceeded = false;
  bool cancelled = false;
  int result = 0;
  alpm_errno_t error = ALPM_ERR_OK;
};

bool isSignatureError(alpm_errno_t error) {
  return error == ALPM_ERR_SIG_INVALID || error == ALPM_ERR_SIG_MISSING || error == ALPM_ERR_DB_INVALID_SIG ||
         error == ALPM_ERR_PKG_INVALID_SIG;
}

}  // namespace

SyncRefresher::SyncRefresher(std::shared_ptr<AlpmSession> session, UpgradeLimits limits, std::uint64_t seeded_bytes,
                             holonight_packages_domain::ProgressSink progress, LogSink journal)
    : session_(std::move(session)),
      limits_(limits),
      seeded_bytes_(seeded_bytes),
      progress_(std::move(progress)),
      journal_(std::move(journal)) {}

namespace {

// Counts the bytes of every database file as libalpm reports them and raises the cap flag as soon as their sum plus
// the seeded copies exceeds the cap (REQ-F-010); sends one Syncing update per finished database (REQ-F-034).
class SyncWatcher {
 public:
  SyncWatcher(std::shared_ptr<SyncState> state, std::uint64_t cap, std::uint64_t seeded, std::uint32_t repositories,
              holonight_packages_domain::ProgressSink progress)
      : state_(std::move(state)),
        cap_(cap),
        seeded_(seeded),
        repositories_(repositories),
        progress_(std::move(progress)) {}

  void onDownload(const std::string& file, alpm_download_event_type_t type, void* data) {
    std::uint64_t fileBytes = 0;
    if (type == ALPM_DOWNLOAD_PROGRESS) {
      const auto* event = static_cast<const alpm_download_event_progress_t*>(data);
      // The total is known from the first event when the server sends a length: an oversized file stops at once.
      fileBytes = static_cast<std::uint64_t>(std::max({event->downloaded, event->total, off_t{0}}));
    } else if (type == ALPM_DOWNLOAD_COMPLETED) {
      const auto* event = static_cast<const alpm_download_event_completed_t*>(data);
      fileBytes = static_cast<std::uint64_t>(std::max<off_t>(event->total, 0));
      if (event->result >= 0 && file.ends_with(".db")) {
        reportFinished(file.substr(0, file.size() - 3));
      }
    } else {
      return;
    }
    auto& known = bytes_[file];
    known = std::max(known, fileBytes);
    std::uint64_t sum = seeded_;
    for (const auto& [name, size] : bytes_) {
      sum += size;
    }
    if (sum > cap_) {
      const std::scoped_lock lock(state_->mutex);
      state_->cap_exceeded = true;
      state_->changed.notify_all();
    }
  }

 private:
  void reportFinished(const std::string& repository) {
    completed_ += 1;
    if (progress_) {
      progress_(ProgressUpdate{
          .phase = ProgressPhase::Syncing,
          .current = completed_,
          .total = repositories_,
          .item = repository,
      });
    }
  }

  std::shared_ptr<SyncState> state_;
  std::uint64_t cap_;
  std::uint64_t seeded_;
  std::uint32_t repositories_;
  holonight_packages_domain::ProgressSink progress_;
  std::map<std::string, std::uint64_t> bytes_;
  std::uint32_t completed_ = 0;
};

}  // namespace

SyncOutcome SyncRefresher::run(PreparationControl& control) {
  auto state = std::make_shared<SyncState>();
  auto watcher = std::make_shared<SyncWatcher>(state, limits_.scratch_cap_bytes, seeded_bytes_,
                                               static_cast<std::uint32_t>(session_->repositories().size()), progress_);
  session_->setDownloadHandler([watcher](const std::string& file, alpm_download_event_type_t type, void* data) {
    watcher->onDownload(file, type, data);
  });
  control.setWaker([state] {
    const std::scoped_lock lock(state->mutex);
    state->cancelled = true;
    state->changed.notify_all();
  });
  if (control.cancelled()) {
    control.setWaker({});
    return SyncOutcome{.failure = refuse(TransactionErrorCode::Cancelled), .worker_stuck = false};
  }

  std::thread([session = session_, state] {
    alpm_handle_t* handle = session->handle();
    const int result = alpm_db_update(handle, alpm_get_syncdbs(handle), 0);
    const alpm_errno_t error = result < 0 ? alpm_errno(handle) : ALPM_ERR_OK;
    const std::scoped_lock lock(state->mutex);
    state->done = true;
    state->result = result;
    state->error = error;
    state->changed.notify_all();
  }).detach();

  std::unique_lock lock(state->mutex);
  const bool settled = state->changed.wait_for(
      lock, limits_.sync_timeout, [&state] { return state->done || state->cap_exceeded || state->cancelled; });
  control.setWaker({});
  const bool stuck = !state->done;
  if (state->cancelled) {
    return SyncOutcome{.failure = refuse(TransactionErrorCode::Cancelled), .worker_stuck = stuck};
  }
  if (!settled || state->cap_exceeded) {
    journal(settled ? "the scratch databases exceed the size cap"
                    : "the database sync did not finish within the internal timeout");
    return SyncOutcome{.failure = refuse(TransactionErrorCode::SyncFailed), .worker_stuck = stuck};
  }
  if (state->result >= 0) {
    return SyncOutcome{};
  }
  // The worker is done: its log is complete and nobody else touches the session.
  const alpm_errno_t error = state->error;
  lock.unlock();
  return SyncOutcome{.failure = refuse(failureCode(error)), .worker_stuck = false};
}

// A database signature that does not verify, or a required signature file that is missing (a failed download of a
// .sig file), is DatabaseUnavailable (REQ-F-011); every other failure is SyncFailed (REQ-F-009).
holonight_packages_domain::TransactionErrorCode SyncRefresher::failureCode(alpm_errno_t error) const {
  const auto& lines = session_->logLines();
  const bool signatureFailure = isSignatureError(error) || logMentions(lines, "required key missing") ||
                                (isDownloadError(error) && logMentions(lines, ".sig'"));
  journal(std::string("the database sync failed: ") + alpm_strerror(error));
  return signatureFailure ? TransactionErrorCode::DatabaseUnavailable : TransactionErrorCode::SyncFailed;
}

void SyncRefresher::journal(const std::string& text) const {
  if (journal_) {
    journal_(LogLevel::Error, text);
  }
}

}  // namespace holonight_packages_transaction
