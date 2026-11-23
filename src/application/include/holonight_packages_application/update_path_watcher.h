#pragma once

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <functional>

namespace holonight_packages_application {

class UpdatePathWatcher : public QObject {
  Q_OBJECT

 public:
  using Paths = std::function<QStringList()>;
  UpdatePathWatcher(Paths paths, std::chrono::milliseconds debounce);
  void start();
  [[nodiscard]] bool pending() const;

 signals:
  void invalidated();
  void reconciled();

 private:
  struct WatchSet {
    QSet<QString> files;
    QSet<QString> directories;
  };

  [[nodiscard]] static WatchSet desiredWatches(const QStringList& paths);
  void discover();
  void apply();

  Paths paths_;
  QFileSystemWatcher watcher_;
  QFutureWatcher<WatchSet> discovery_;
  QTimer timer_;
  bool discovering_ = false;
  bool superseded_ = false;
  bool verify_registration_ = false;
};

}  // namespace holonight_packages_application
