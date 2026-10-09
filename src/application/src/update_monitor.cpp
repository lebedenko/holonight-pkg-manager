#include "holonight_packages_application/update_monitor.h"

#include "holonight_packages_application/snapshot_selection.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrentRun>

#include <exception>
#include <utility>

namespace holonight_packages_application {

namespace {
void appendDirectoryContents(const QFileInfo& info, QStringList& paths) {
  if (!info.isDir()) {
    return;
  }
  QDirIterator entries(info.absoluteFilePath(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
  while (entries.hasNext()) {
    paths.push_back(entries.next());
  }
}
}  // namespace

UpdateMonitor::UpdateMonitor(std::shared_ptr<holonight_packages_domain::UpdateSource> source,
                             UpdateMonitorOptions options, QObject* parent)
    : QObject(parent), source_(std::move(source)), options_(std::move(options)) {
  debounce_timer_.setSingleShot(true);
  debounce_timer_.setInterval(options_.debounce);
  connect(&debounce_timer_, &QTimer::timeout, this, &UpdateMonitor::refresh);
  connect(&fs_watcher_, &QFileSystemWatcher::directoryChanged, this, &UpdateMonitor::onFilesystemChanged);
  connect(&fs_watcher_, &QFileSystemWatcher::fileChanged, this, &UpdateMonitor::onFilesystemChanged);
  connect(&evaluation_watcher_, &QFutureWatcher<LoadResult>::finished, this, &UpdateMonitor::onEvaluationFinished);
}

UpdateMonitor::~UpdateMonitor() {
  // The worker holds its own shared_ptr to the source, so waiting here only avoids delivering into a dead watcher.
  evaluation_watcher_.disconnect(this);
  evaluation_watcher_.waitForFinished();
}

void UpdateMonitor::start() {
  armWatcher();
  refresh();
}

void UpdateMonitor::refresh() {
  debounce_timer_.stop();
  if (running_) {
    rerun_ = true;
    return;
  }
  startEvaluation();
}

// Keep an existing ancestor watched as well, so missing or removed paths can be discovered without polling.
// Reconcile the watches after every event as directories are created, removed or replaced by rename.
void UpdateMonitor::armWatcher() {
  QStringList desired;
  QStringList paths = options_.watchPaths;
  for (const auto& path : source_->watchPaths()) {
    if (!path.empty()) {
      paths.append(QString::fromStdString(path.string()));
    }
  }
  for (const QString& path : paths) {
    const QFileInfo info(path);
    if (info.exists()) {
      desired.push_back(info.absoluteFilePath());
      appendDirectoryContents(info, desired);
    }
    QDir ancestor = info.absoluteDir();
    while (!ancestor.exists()) {
      const QDir parent = QFileInfo(ancestor.absolutePath()).absoluteDir();
      if (parent.absolutePath() == ancestor.absolutePath()) {
        break;
      }
      ancestor = parent;
    }
    if (ancestor.exists()) {
      desired.push_back(ancestor.absolutePath());
    }
  }
  desired.removeDuplicates();
  const QStringList watched = fs_watcher_.directories() + fs_watcher_.files();
  QStringList obsolete;
  for (const QString& path : watched) {
    if (!desired.contains(path)) {
      obsolete.push_back(path);
    }
  }
  if (!obsolete.isEmpty()) {
    fs_watcher_.removePaths(obsolete);
  }
  QStringList missing;
  for (const QString& path : desired) {
    if (!watched.contains(path)) {
      missing.push_back(path);
    }
  }
  if (!missing.isEmpty()) {
    fs_watcher_.addPaths(missing);
  }
}

void UpdateMonitor::onFilesystemChanged() {
  if (running_) {
    dirty_ = true;
  }
  armWatcher();
  debounce_timer_.start();
}

void UpdateMonitor::startEvaluation() {
  running_ = true;
  armWatcher();
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
  const LoadResult result = evaluation_watcher_.result();
  running_ = false;
  if (std::exchange(dirty_, false)) {
    rerun_ = false;
    startEvaluation();
    return;
  }

  if (result.has_value()) {
    local_ = *result;
  }
  if (online_.has_value() && result.has_value()) {
    applyFreshest();
  } else {
    publish(buildUpdateStatus(result, status_));
  }

  if (rerun_) {
    rerun_ = false;
    startEvaluation();
  }
}

void UpdateMonitor::adoptOnline(const holonight_packages_domain::CheckedSnapshot& snapshot) {
  online_ = snapshot.snapshot;
  if (snapshot.snapshot.repositories.empty()) {
    applyFreshest();
  } else {
    dirty_ = running_;
    refresh();
  }
}

void UpdateMonitor::applyFreshest() {
  const bool online = selectFresherSnapshot(local_, online_) == SnapshotChoice::Online;
  const holonight_packages_domain::UpdateSnapshot& chosen = online ? *online_ : *local_;
  publish(buildUpdateStatus(chosen, status_));
}

void UpdateMonitor::publish(const UpdateStatus& updated) {
  if (updated != status_) {
    status_ = updated;
    emit statusChanged(status_);
  }
}

}  // namespace holonight_packages_application
