#pragma once

#include "holonight_packages_application/update_status.h"
#include "holonight_packages_domain/update_source.h"

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <expected>
#include <memory>

namespace holonight_packages_application {

struct UpdateMonitorOptions {
  // Directories (or files) whose changes mean the update status may be stale.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  QStringList watchPaths;
  // Quiet period after the last filesystem change before an evaluation starts.
  std::chrono::milliseconds debounce{2000};
};

// Keeps an UpdateStatus current. Evaluations run UpdateSource::loadUpdates() on a worker thread; at most one runs at
// a time and triggers that arrive meanwhile coalesce into one follow-up. Nothing runs without a trigger.
class UpdateMonitor : public QObject {
  Q_OBJECT

 public:
  UpdateMonitor(std::shared_ptr<holonight_packages_domain::UpdateSource> source, UpdateMonitorOptions options,
                QObject* parent = nullptr);
  ~UpdateMonitor() override;

  UpdateMonitor(const UpdateMonitor&) = delete;
  UpdateMonitor& operator=(const UpdateMonitor&) = delete;
  UpdateMonitor(UpdateMonitor&&) = delete;
  UpdateMonitor& operator=(UpdateMonitor&&) = delete;

  [[nodiscard]] const UpdateStatus& status() const { return status_; }

  // Arms the filesystem watcher and runs the first evaluation.
  void start();
  // Runs an evaluation now (no debounce), or schedules one follow-up when one is already running.
  void refresh();

 signals:
  void statusChanged(const holonight_packages_application::UpdateStatus& status);

 private:
  using LoadResult =
      std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateSourceError>;

  void armWatcher();
  void onFilesystemChanged();
  void startEvaluation();
  void onEvaluationFinished();

  std::shared_ptr<holonight_packages_domain::UpdateSource> source_;
  UpdateMonitorOptions options_;
  QFileSystemWatcher fs_watcher_;
  QTimer debounce_timer_;
  QFutureWatcher<LoadResult> evaluation_watcher_;
  UpdateStatus status_;
  bool running_ = false;
  bool rerun_ = false;
};

}  // namespace holonight_packages_application
