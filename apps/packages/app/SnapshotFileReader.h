#pragma once

#include "holonight_packages_domain/update_checker.h"
#include "holonight_packages_domain/update_snapshot_store.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

// Read-only view of the snapshot file that holonight-packaged writes. It only ever calls UpdateSnapshotStore::load()
// (the store is held as const), so a reader can never delete or rewrite a good file. An absent or corrupt file is
// tolerated: cached metadata is invalidated on transition and the GUI retains its current rows.
class SnapshotFileReader : public QObject {
  Q_OBJECT

 public:
  SnapshotFileReader(std::shared_ptr<const holonight_packages_domain::UpdateSnapshotStore> store,
                     std::filesystem::path file, std::chrono::milliseconds debounce = std::chrono::milliseconds{150},
                     QObject* parent = nullptr);

  // Arms the watcher and reads once.
  void start();
  // Reads immediately (cancels a pending debounced read).
  void readNow();
  [[nodiscard]] const std::optional<holonight_packages_domain::CheckedSnapshot>& latest() const { return latest_; }

 signals:
  // A valid snapshot that differs from the previous one.
  void snapshotInvalidated();
  void snapshotRead(const holonight_packages_domain::CheckedSnapshot& snapshot);

 private:
  void armWatcher();
  void onFilesystemChanged();

  std::shared_ptr<const holonight_packages_domain::UpdateSnapshotStore> store_;
  std::filesystem::path file_;
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  std::optional<holonight_packages_domain::CheckedSnapshot> latest_;
};
