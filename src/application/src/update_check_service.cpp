#include "holonight_packages_application/update_check_service.h"

#include "holonight_packages_application/update_summary.h"

#include <QMetaObject>
#include <QThread>
#include <QtConcurrentRun>
#include <QtLogging>

#include <exception>
#include <utility>

namespace holonight_packages_application {

namespace {

using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::SnapshotFileState;
using holonight_packages_domain::UpdateCheckError;
using holonight_packages_domain::UpdateCheckErrorCode;

const char* originName(CheckOrigin origin) { return origin == CheckOrigin::OnDemand ? "on-demand" : "automatic"; }

}  // namespace

UpdateCheckService::UpdateCheckService(Dependencies dependencies, UpdateCheckPolicy policy, QObject* parent)
    : QObject(parent), dependencies_(std::move(dependencies)), policy_(policy) {
  pool_.setMaxThreadCount(1);
  connect(&watcher_, &QFutureWatcher<WorkerResult>::finished, this, &UpdateCheckService::onWorkerFinished);
}

UpdateCheckService::~UpdateCheckService() {
  // The worker holds its own shared_ptrs, so waiting only avoids delivering into a dead watcher.
  watcher_.disconnect(this);
  pool_.waitForDone();
}

void UpdateCheckService::start() {
  if (const auto history = dependencies_.store->loadHistory()) {
    UpdateCheckStatus updated = status_;
    updated.hasCheckResult = true;
    updated.lastCheckTime = history->completed;
    updated.lastCheckSucceeded = history->succeeded;
    updated.lastError = history->error;
    last_completed_ = history->completed;
    setStatus(updated);
  }
  const auto loaded = dependencies_.store->load();
  if (!loaded) {
    qWarning().noquote() << "update check: cannot read the stored snapshot:"
                         << QString::fromStdString(loaded.error().message);
    return;
  }
  if (loaded->state == SnapshotFileState::Invalid) {
    qInfo() << "update check: discarding an invalid stored snapshot";
    if (const auto discarded = dependencies_.store->discardInvalid(); !discarded) {
      qWarning().noquote() << "update check: cannot discard the invalid snapshot:"
                           << QString::fromStdString(discarded.error().message);
    }
    return;
  }
  if (loaded->state == SnapshotFileState::Valid && loaded->snapshot.has_value()) {
    adoptSnapshot(*loaded->snapshot);
  }
}

void UpdateCheckService::adoptSnapshot(const CheckedSnapshot& snapshot) {
  last_good_ = snapshot;
  UpdateCheckStatus updated = status_;
  updated.snapshotFetchedAt = snapshot.fetchedAt;
  updated.count = summarizeUpdates(snapshot.snapshot.updates).updateCount;
  setStatus(updated);
  emit snapshotAdopted(snapshot);
}

void UpdateCheckService::requestCheck(CheckOrigin origin, std::function<void(const UpdateCheckOutcome&)> onDone) {
  if (QThread::currentThread() == thread()) {
    handleRequest(origin, std::move(onDone));
    return;
  }
  QMetaObject::invokeMethod(
      this, [this, origin, done = std::move(onDone)] mutable { handleRequest(origin, std::move(done)); },
      Qt::QueuedConnection);
}

void UpdateCheckService::handleRequest(CheckOrigin origin, std::function<void(const UpdateCheckOutcome&)> onDone) {
  if (!dependencies_.capabilities.canCheckForUpdates) {
    return;
  }
  if (running_) {
    if (onDone) {
      joined_.push_back(std::move(onDone));
    }
    return;
  }
  if (origin == CheckOrigin::OnDemand && last_completed_.has_value() &&
      dependencies_.clock->now() - *last_completed_ < policy_.onDemandCooldown) {
    return;
  }
  if (onDone) {
    joined_.push_back(std::move(onDone));
  }
  beginCheck(origin);
}

void UpdateCheckService::beginCheck(CheckOrigin origin) {
  running_ = true;
  running_origin_ = origin;
  started_at_ = std::chrono::steady_clock::now();
  qInfo() << "update check started, origin" << originName(origin);

  UpdateCheckStatus updated = status_;
  updated.checking = true;
  setStatus(updated);

  watcher_.setFuture(QtConcurrent::run(&pool_, [deps = dependencies_] -> WorkerResult {
    WorkerResult worker{};
    try {
      worker.result = deps.checker->checkForUpdates();
    } catch (const std::exception&) {
      worker.result = std::unexpected(UpdateCheckError{UpdateCheckErrorCode::Unknown});
    }
    worker.completedAt = deps.clock->now();
    if (worker.result.has_value()) {
      const auto saved = deps.store->save(CheckedSnapshot{.snapshot = *worker.result, .fetchedAt = worker.completedAt});
      if (!saved) {
        worker.saveError = saved.error().message;
      }
    }
    return worker;
  }));
}

void UpdateCheckService::onWorkerFinished() {
  const WorkerResult worker = watcher_.result();
  auto joined = std::exchange(joined_, {});
  running_ = false;
  const auto durationMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at_).count();

  UpdateCheckOutcome outcome{};
  outcome.completedAt = worker.completedAt;
  outcome.origin = running_origin_;
  outcome.succeeded = worker.result.has_value() && !worker.saveError.has_value();
  if (!outcome.succeeded) {
    outcome.error = worker.result ? UpdateCheckErrorCode::Unknown : worker.result.error().code;
  }

  UpdateCheckStatus updated = status_;
  updated.checking = false;
  updated.hasCheckResult = true;
  updated.lastCheckSucceeded = outcome.succeeded;
  updated.lastError = outcome.error;
  updated.lastCheckTime = worker.completedAt;
  last_completed_ = worker.completedAt;

  if (worker.saveError.has_value()) {
    qWarning().noquote() << "update check: cannot save the snapshot:" << QString::fromStdString(*worker.saveError);
  }
  qInfo().noquote() << "update check finished, origin" << originName(outcome.origin) << "outcome"
                    << (outcome.succeeded
                            ? "success"
                            : QString::fromUtf8(holonight_packages_domain::updateCheckErrorName(*outcome.error)))
                    << "duration_ms" << durationMs;

  if (outcome.succeeded) {
    last_good_ = CheckedSnapshot{.snapshot = *worker.result, .fetchedAt = worker.completedAt};
    updated.snapshotFetchedAt = worker.completedAt;
    updated.count = summarizeUpdates(last_good_->snapshot.updates).updateCount;
  }
  if (const auto saved = dependencies_.store->saveHistory(
          {.completed = outcome.completedAt, .succeeded = outcome.succeeded, .error = outcome.error});
      !saved) {
    qWarning() << "update check: cannot save check history";
  }
  const auto completedSnapshot = last_good_;
  setStatus(updated);
  if (outcome.succeeded) {
    emit snapshotAdopted(*completedSnapshot);
  }
  emit checkCompleted(outcome);

  for (const auto& callback : joined) {
    callback(outcome);
  }
}

void UpdateCheckService::setStatus(const UpdateCheckStatus& status) {
  if (status == status_) {
    return;
  }
  status_ = status;
  emit statusChanged(status_);
}

}  // namespace holonight_packages_application
