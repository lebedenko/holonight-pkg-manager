#include "holonight_packages_application/update_monitor.h"

#include <QtConcurrentRun>

#include <exception>
#include <utility>

namespace holonight_packages_application {

UpdateMonitor::UpdateMonitor(std::shared_ptr<holonight_packages_domain::UpdateSource> source,
                             UpdateMonitorOptions options, QObject* parent)
    : QObject(parent),
      source_(std::move(source)),
      options_(std::move(options)),
      fs_watcher_(
          [source = source_, paths = options_.watchPaths] {
            auto desired = paths;
            for (const auto& path : source->watchPaths()) {
              if (!path.empty()) {
                desired.append(QString::fromStdString(path.string()));
              }
            }
            return desired;
          },
          options_.debounce) {
  connect(&fs_watcher_, &holonight_packages_application::UpdatePathWatcher::invalidated, this,
          [this] { snapshots_.invalidateInFlightEvaluation(running_); });
  connect(&fs_watcher_, &holonight_packages_application::UpdatePathWatcher::reconciled, this, &UpdateMonitor::refresh);
  connect(&evaluation_watcher_, &QFutureWatcher<LoadResult>::finished, this, &UpdateMonitor::onEvaluationFinished);
}

UpdateMonitor::~UpdateMonitor() {
  // The worker holds its own shared_ptr to the source, so waiting here only avoids delivering into a dead watcher.
  evaluation_watcher_.disconnect(this);
  evaluation_watcher_.waitForFinished();
}

void UpdateMonitor::start() { fs_watcher_.start(); }

void UpdateMonitor::refresh() {
  if (running_) {
    rerun_ = true;
    return;
  }
  startEvaluation();
}

void UpdateMonitor::startEvaluation() {
  running_ = true;
  evaluation_watcher_.setFuture(QtConcurrent::run([source = source_] -> LoadResult {
    try {
      return source->loadUpdates();
    } catch (const std::exception& error) {
      return std::unexpected(holonight_packages_domain::UpdateSourceError{
          .code = holonight_packages_domain::UpdateSourceErrorCode::Unknown,
          .message = error.what(),
      });
    }
  }));
}

void UpdateMonitor::onEvaluationFinished() {
  LoadResult result = evaluation_watcher_.future().takeResult();
  running_ = false;
  if (snapshots_.takeInvalidation()) {
    if (std::exchange(rerun_, false) || !fs_watcher_.pending()) {
      startEvaluation();
    }
    return;
  }

  if (result.has_value()) {
    snapshots_.recordEvaluation(std::move(*result));
    publish(buildUpdateStatus(snapshots_.selected(), status_));
  } else {
    publish(buildUpdateStatus(result, status_));
  }

  if (rerun_) {
    rerun_ = false;
    startEvaluation();
  }
}

void UpdateMonitor::adoptOnline(const holonight_packages_domain::CheckedSnapshot& snapshot) {
  snapshots_.adoptChecked(snapshot);
  if (!snapshots_.evaluation()) {
    publish(buildUpdateStatus(snapshots_.checked()->snapshot, status_));
  }
  snapshots_.invalidateInFlightEvaluation(running_);
  refresh();
}

void UpdateMonitor::publish(const UpdateStatus& updated) {
  if (updated != status_) {
    status_ = updated;
    emit statusChanged(status_);
  }
}

}  // namespace holonight_packages_application
