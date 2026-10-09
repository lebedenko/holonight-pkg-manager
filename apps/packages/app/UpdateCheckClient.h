#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

// What the GUI knows about the online update check that holonight-packaged runs. Mirrors the
// org.holonight.Packages1.UpdateCheck properties; the update list itself never travels here.
struct UpdateCheckClientStatus {
  // The service currently owns its bus name. False while it is not running (it is started on demand by CheckNow).
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool serviceReachable = false;
  // The backend can check online. Only meaningful while the service is reachable.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool canCheck = true;
  bool checking = false;
  // Unix seconds of the latest attempt's completion; 0 = never checked.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  qint64 lastCheckTime = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool lastCheckSucceeded = false;
  // "", "network-unavailable", "repository-unreachable", "busy" or "unknown".
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  QString lastCheckError;
  // Unix seconds of the last successful check; 0 = no snapshot.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  qint64 snapshotFetchedAt = 0;

  bool operator==(const UpdateCheckClientStatus&) const = default;
};

// Port to the update-check service. The D-Bus implementation is DBusUpdateCheckClient; tests use a fake.
class UpdateCheckClient : public QObject {
  Q_OBJECT

 public:
  using QObject::QObject;

  // Asks the service for an on-demand check and returns immediately. Starts the service when it is not running.
  virtual void checkNow() = 0;
  [[nodiscard]] virtual const UpdateCheckClientStatus& status() const = 0;

 signals:
  void statusChanged();
  // A check finished (the service's StatusChanged signal), whatever its outcome.
  void checkCompleted();
  // The CheckNow call itself failed (service not activatable, crashed, timed out).
  void checkNowFailed();
  void checkNowSucceeded();
};
