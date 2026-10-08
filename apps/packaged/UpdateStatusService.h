#pragma once

#include "holonight_packages_application/update_monitor.h"

#include <QDBusConnection>
#include <QObject>
#include <QString>

// Exports an UpdateMonitor's status on D-Bus as org.holonight.Packages1.Updates.
class UpdateStatusService : public QObject {
  Q_OBJECT

 public:
  static constexpr const char* kServiceName = "org.holonight.Packages1";
  static constexpr const char* kObjectPath = "/org/holonight/Packages1";
  static constexpr const char* kInterfaceName = "org.holonight.Packages1.Updates";

  UpdateStatusService(holonight_packages_application::UpdateMonitor* monitor, QObject* parent = nullptr);

  [[nodiscard]] const holonight_packages_application::UpdateStatus& status() const { return monitor_->status(); }
  void refresh() { monitor_->refresh(); }

  // Registers the object and then the well-known name. Returns false when the name is already owned or the
  // registration fails; the reason is available from error().
  [[nodiscard]] bool registerOn(const QDBusConnection& connection);
  [[nodiscard]] const QString& error() const { return error_; }

 private:
  void emitPropertiesChanged();

  holonight_packages_application::UpdateMonitor* monitor_;
  QDBusConnection connection_;
  QString error_;
};
