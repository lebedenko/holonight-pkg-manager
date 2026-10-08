#pragma once

#include "UpdateStatusService.h"

#include <QDBusAbstractAdaptor>
#include <QString>

// Thin mapping from the service's UpdateStatus to the properties in dbus/org.holonight.Packages1.Updates.xml. The
// interface exposes no method that takes a command, path or package name.
class UpdatesAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.holonight.Packages1.Updates")
  Q_PROPERTY(QString State READ state)
  Q_PROPERTY(uint Count READ count)
  Q_PROPERTY(uint IgnoredCount READ ignoredCount)
  Q_PROPERTY(qulonglong DownloadSizeBytes READ downloadSizeBytes)
  Q_PROPERTY(qlonglong DataAsOf READ dataAsOf)
  Q_PROPERTY(QString LastError READ lastError)

 public:
  explicit UpdatesAdaptor(UpdateStatusService* service);

  [[nodiscard]] QString state() const;
  [[nodiscard]] uint count() const;
  [[nodiscard]] uint ignoredCount() const;
  [[nodiscard]] qulonglong downloadSizeBytes() const;
  [[nodiscard]] qlonglong dataAsOf() const;
  [[nodiscard]] QString lastError() const;

 public slots:
  // NOLINTNEXTLINE(readability-identifier-naming): the slot name is the D-Bus method name.
  void Refresh();

 private:
  UpdateStatusService* service_;
};
