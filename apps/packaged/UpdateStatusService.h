#pragma once

#include "holonight_packages_application/update_check_service.h"
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

  static constexpr const char* kCheckInterfaceName = "org.holonight.Packages1.UpdateCheck";

  // Exports only the Updates interface.
  explicit UpdateStatusService(holonight_packages_application::UpdateMonitor* monitor, QObject* parent = nullptr);
  // Additionally exports the UpdateCheck interface backed by `check`.
  UpdateStatusService(holonight_packages_application::UpdateMonitor* monitor,
                      holonight_packages_application::UpdateCheckService* check, QObject* parent = nullptr);

  [[nodiscard]] holonight_packages_application::UpdateCheckService* checkService() const { return check_; }
  // Requests an on-demand check and returns immediately.
  void requestOnDemandCheck();

  [[nodiscard]] const holonight_packages_application::UpdateStatus& status() const { return monitor_->status(); }
  void refresh() { monitor_->refresh(); }

  // Registers the object and then the well-known name. Returns false when the name is already owned or the
  // registration fails; the reason is available from error().
  [[nodiscard]] bool registerOn(const QDBusConnection& connection);
  [[nodiscard]] const QString& error() const { return error_; }

 private:
  void emitPropertiesChanged();
  void emitCheckPropertiesChanged();

  holonight_packages_application::UpdateMonitor* monitor_;
  holonight_packages_application::UpdateCheckService* check_ = nullptr;
  QDBusConnection connection_;
  QString error_;
};
