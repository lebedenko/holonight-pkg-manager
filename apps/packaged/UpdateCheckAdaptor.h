#pragma once

#include "UpdateStatusService.h"

#include <QDBusAbstractAdaptor>
#include <QString>

// org.holonight.Packages1.UpdateCheck: requests an online check and mirrors its status. The only method takes no
// argument, and the update list itself never crosses the bus (it is handed over through the snapshot file).
class UpdateCheckAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.holonight.Packages1.UpdateCheck")
  Q_PROPERTY(bool CanCheck READ canCheck)
  Q_PROPERTY(bool Checking READ checking)
  Q_PROPERTY(qlonglong LastCheckTime READ lastCheckTime)
  Q_PROPERTY(bool LastCheckSucceeded READ lastCheckSucceeded)
  Q_PROPERTY(QString LastCheckError READ lastCheckError)
  Q_PROPERTY(qlonglong SnapshotFetchedAt READ snapshotFetchedAt)

 public:
  explicit UpdateCheckAdaptor(UpdateStatusService* service);

  [[nodiscard]] bool canCheck() const;
  [[nodiscard]] bool checking() const;
  [[nodiscard]] qlonglong lastCheckTime() const;
  [[nodiscard]] bool lastCheckSucceeded() const;
  [[nodiscard]] QString lastCheckError() const;
  [[nodiscard]] qlonglong snapshotFetchedAt() const;

 public slots:
  // NOLINTNEXTLINE(readability-identifier-naming): the slot name is the D-Bus method name.
  void CheckNow();

 signals:
  // NOLINTNEXTLINE(readability-identifier-naming): the signal name is the D-Bus signal name.
  void StatusChanged();

 private:
  UpdateStatusService* service_;
};
