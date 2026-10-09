#pragma once

#include "SnapshotFileReader.h"
#include "UpdateCheckClient.h"
#include "UpdatesModel.h"
#include "holonight_packages_application/update_check_ports.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <chrono>
#include <functional>
#include <memory>

// View-model for the "Check now" control and the status line on the Updates page. It talks to holonight-packaged only
// through the UpdateCheckClient port and reads the update list only through SnapshotFileReader. It exposes text and
// flags; it opens no popup, dialog or notification.
class UpdateCheckModel : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(UpdateCheckModel)
  QML_UNCREATABLE("UpdateCheckModel is provided by the application")
  // False when the service is reachable and its backend cannot check online: the control is then hidden.
  Q_PROPERTY(bool available READ available NOTIFY changed)
  // The last CheckNow() call failed at D-Bus level (service not activatable, crashed, timed out).
  Q_PROPERTY(bool serviceUnavailable READ serviceUnavailable NOTIFY changed)
  Q_PROPERTY(bool checking READ checking NOTIFY changed)
  Q_PROPERTY(bool checkNowEnabled READ checkNowEnabled NOTIFY changed)
  // True while a valid persisted online snapshot is available, including when local data is displayed.
  Q_PROPERTY(bool hasSnapshot READ hasSnapshot NOTIFY changed)
  Q_PROPERTY(QDateTime snapshotFetchedAt READ snapshotFetchedAt NOTIFY changed)
  Q_PROPERTY(QDateTime displayedSnapshotFetchedAt READ displayedSnapshotFetchedAt NOTIFY changed)
  // "Last checked 3 h ago"; empty unless a valid online snapshot supplies the displayed rows.
  Q_PROPERTY(QString snapshotAgeText READ snapshotAgeText NOTIFY changed)
  // Displayed online age, "Showing local package data", or "Not checked yet".
  Q_PROPERTY(QString sourceText READ sourceText NOTIFY changed)
  Q_PROPERTY(QString checkHistoryText READ checkHistoryText NOTIFY changed)
  Q_PROPERTY(QString statusLineText READ statusLineText NOTIFY changed)
  Q_PROPERTY(QDateTime lastCheckTime READ lastCheckTime NOTIFY changed)
  Q_PROPERTY(bool lastCheckSucceeded READ lastCheckSucceeded NOTIFY changed)
  // A check result exists and it failed.
  Q_PROPERTY(bool failed READ failed NOTIFY changed)
  Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY changed)
  Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY changed)
  // "Last check failed: <message>", "Update service unavailable", or empty.
  Q_PROPERTY(QString failureText READ failureText NOTIFY changed)

 public:
  using Clock = std::function<std::chrono::system_clock::time_point()>;

  UpdateCheckModel(UpdateCheckClient* client, SnapshotFileReader* reader, UpdatesModel* updates, Clock now,
                   std::unique_ptr<holonight_packages_application::OneShotTimer> ageTimer, QObject* parent = nullptr);

  [[nodiscard]] bool available() const;
  [[nodiscard]] bool serviceUnavailable() const { return service_unavailable_; }
  [[nodiscard]] bool checking() const;
  [[nodiscard]] bool checkNowEnabled() const;
  [[nodiscard]] bool hasSnapshot() const;
  [[nodiscard]] QDateTime snapshotFetchedAt() const;
  [[nodiscard]] QDateTime displayedSnapshotFetchedAt() const;
  [[nodiscard]] QString snapshotAgeText() const;
  [[nodiscard]] QString statusLineText() const;
  [[nodiscard]] QString checkHistoryText() const;
  [[nodiscard]] QString sourceText() const { return updates_->sourceText(); }
  [[nodiscard]] QDateTime lastCheckTime() const;
  [[nodiscard]] bool lastCheckSucceeded() const;
  [[nodiscard]] bool failed() const;
  [[nodiscard]] QString lastErrorCode() const;
  [[nodiscard]] QString lastErrorMessage() const;
  [[nodiscard]] QString failureText() const;

  // Exactly one client call per activation.
  Q_INVOKABLE void checkNow();

 signals:
  void changed();

 private:
  void onClientStatusChanged();
  void armAgeTimer();
  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> fetchedAt() const;

  UpdateCheckClient* client_;
  SnapshotFileReader* reader_;
  UpdatesModel* updates_;
  Clock now_;
  std::unique_ptr<holonight_packages_application::OneShotTimer> age_timer_;
  bool service_unavailable_ = false;
};
