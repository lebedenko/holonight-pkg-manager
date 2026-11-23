#include "SnapshotFileReader.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QtConcurrentRun>

#include <utility>

SnapshotFileReader::SnapshotFileReader(std::shared_ptr<const holonight_packages_domain::UpdateSnapshotStore> store,
                                       std::filesystem::path file, std::chrono::milliseconds debounce, QObject* parent)
    : QObject(parent), store_(std::move(store)), file_(std::move(file)) {
  connect(&read_watcher_, &QFutureWatcher<Snapshot>::finished, this, &SnapshotFileReader::onReadFinished);
  debounce_.setSingleShot(true);
  debounce_.setInterval(debounce);
  connect(&debounce_, &QTimer::timeout, this, &SnapshotFileReader::readNow);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &SnapshotFileReader::onFilesystemChanged);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &SnapshotFileReader::onFilesystemChanged);
}

void SnapshotFileReader::start() { readNow(); }

// Watches the nearest existing ancestor of the file, plus the file itself and its directory once they exist, so a
// first-ever write (or a directory created later) is noticed without polling. The writer replaces the file by
// rename, so the directory watch is what fires; the file watch is a courtesy for in-place edits.
void SnapshotFileReader::armWatcher() {
  QStringList desired;
  const QFileInfo file(QString::fromStdString(file_.string()));
  if (file.exists()) {
    desired.push_back(file.absoluteFilePath());
  }
  QDir ancestor = file.absoluteDir();
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
  desired.removeDuplicates();

  const QStringList watched = watcher_.directories() + watcher_.files();
  QStringList obsolete;
  for (const QString& path : watched) {
    if (!desired.contains(path)) {
      obsolete.push_back(path);
    }
  }
  QStringList missing;
  for (const QString& path : desired) {
    if (!watched.contains(path)) {
      missing.push_back(path);
    }
  }
  if (!missing.isEmpty()) {
    watcher_.addPaths(missing);
  }
  if (!obsolete.isEmpty()) {
    watcher_.removePaths(obsolete);
  }
}

void SnapshotFileReader::onFilesystemChanged() {
  superseded_ = reading_;
  debounce_.start();
}

void SnapshotFileReader::readNow() {
  debounce_.stop();
  armWatcher();
  if (reading_) {
    superseded_ = true;
    return;
  }
  reading_ = true;
  read_watcher_.setFuture(QtConcurrent::run([store = store_] -> Snapshot {
    try {
      auto loaded = store->load();
      if (loaded && loaded->state == holonight_packages_domain::SnapshotFileState::Valid) {
        return std::move(loaded->snapshot);
      }
    } catch (...) {
      // A failed read has the same presentation as an invalid snapshot.
    }
    return std::nullopt;
  }));
}

void SnapshotFileReader::onReadFinished() {
  reading_ = false;
  if (std::exchange(superseded_, false)) {
    if (!debounce_.isActive()) {
      readNow();
    }
    return;
  }
  auto loaded = read_watcher_.future().takeResult();
  if (latest_ == loaded) {
    return;
  }
  latest_ = std::move(loaded);
  if (latest_) {
    emit snapshotRead(*latest_);
  } else {
    emit snapshotInvalidated();
  }
}
