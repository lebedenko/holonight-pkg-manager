#include "SnapshotFileReader.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <utility>

SnapshotFileReader::SnapshotFileReader(std::shared_ptr<const holonight_packages_domain::UpdateSnapshotStore> store,
                                       std::filesystem::path file, std::chrono::milliseconds debounce, QObject* parent)
    : QObject(parent), store_(std::move(store)), file_(std::move(file)) {
  debounce_.setSingleShot(true);
  debounce_.setInterval(debounce);
  connect(&debounce_, &QTimer::timeout, this, &SnapshotFileReader::readNow);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &SnapshotFileReader::onFilesystemChanged);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &SnapshotFileReader::onFilesystemChanged);
}

void SnapshotFileReader::start() {
  armWatcher();
  readNow();
}

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
  if (!obsolete.isEmpty()) {
    watcher_.removePaths(obsolete);
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
}

void SnapshotFileReader::onFilesystemChanged() {
  armWatcher();
  debounce_.start();
}

void SnapshotFileReader::readNow() {
  debounce_.stop();
  armWatcher();
  const auto loaded = store_->load();
  if (!loaded || loaded->state != holonight_packages_domain::SnapshotFileState::Valid ||
      !loaded->snapshot.has_value()) {
    if (latest_) {
      latest_.reset();
      emit snapshotInvalidated();
    }
    return;
  }
  if (latest_ == loaded->snapshot) {
    return;
  }
  latest_ = loaded->snapshot;
  emit snapshotRead(*latest_);
}
