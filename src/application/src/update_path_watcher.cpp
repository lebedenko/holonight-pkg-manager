#include "holonight_packages_application/update_path_watcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrentRun>

#include <utility>

namespace holonight_packages_application {

UpdatePathWatcher::UpdatePathWatcher(Paths paths, std::chrono::milliseconds debounce) : paths_(std::move(paths)) {
  timer_.setSingleShot(true);
  timer_.setInterval(debounce);
  connect(&timer_, &QTimer::timeout, this, &UpdatePathWatcher::discover);
  connect(&discovery_, &QFutureWatcher<WatchSet>::finished, this, &UpdatePathWatcher::apply);
  const auto changed = [this] {
    superseded_ = discovering_;
    timer_.start();
    emit invalidated();
  };
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, changed);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, changed);
}

void UpdatePathWatcher::start() { discover(); }

bool UpdatePathWatcher::pending() const { return discovering_ || timer_.isActive(); }

void UpdatePathWatcher::discover() {
  if (discovering_) {
    superseded_ = true;
    return;
  }
  discovering_ = true;
  discovery_.setFuture(QtConcurrent::run([paths = paths_] { return desiredWatches(paths()); }));
}

UpdatePathWatcher::WatchSet UpdatePathWatcher::desiredWatches(const QStringList& paths) {
  WatchSet desired;
  const auto add = [&desired](const QFileInfo& info) {
    (info.isDir() ? desired.directories : desired.files).insert(info.absoluteFilePath());
  };
  for (const auto& path : paths) {
    const QFileInfo info(path);
    if (info.exists()) {
      add(info);
    }
    if (info.isDir()) {
      QDirIterator entries(info.absoluteFilePath(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
      while (entries.hasNext()) {
        entries.next();
        add(entries.fileInfo());
      }
    }
    auto ancestor = info.absoluteDir();
    while (!ancestor.exists()) {
      const auto parent = QFileInfo(ancestor.absolutePath()).absoluteDir();
      if (parent == ancestor) {
        break;
      }
      ancestor = parent;
    }
    if (ancestor.exists()) {
      desired.directories.insert(ancestor.absolutePath());
    }
  }
  return desired;
}

void UpdatePathWatcher::apply() {
  discovering_ = false;
  if (std::exchange(superseded_, false)) {
    if (!timer_.isActive()) {
      discover();
    }
    return;
  }
  const auto desired = discovery_.future().takeResult();
  const auto directories = watcher_.directories();
  const auto files = watcher_.files();
  const QSet<QString> current_directories(directories.begin(), directories.end());
  const QSet<QString> current_files(files.begin(), files.end());
  const auto new_directories = (desired.directories - current_directories).values();
  const auto new_files = (desired.files - current_files).values();
  const auto obsolete = ((current_directories | current_files) - (desired.directories | desired.files)).values();
  const bool added_directory =
      !new_directories.isEmpty() && watcher_.addPaths(new_directories).size() < new_directories.size();
  if (!new_files.isEmpty()) {
    watcher_.addPaths(new_files);
  }
  if (added_directory && !std::exchange(verify_registration_, true)) {
    discover();
    return;
  }
  if (!obsolete.isEmpty()) {
    watcher_.removePaths(obsolete);
  }
  verify_registration_ = false;
  emit reconciled();
}

}  // namespace holonight_packages_application
