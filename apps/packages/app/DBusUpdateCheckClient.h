#pragma once

#include "UpdateCheckClient.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QVariantMap>

// QtDBus implementation of UpdateCheckClient. Follows UpdateStatusClient (service watcher, GetAll on appearance,
// PropertiesChanged) but, unlike it, lets D-Bus activation start holonight-packaged for the CheckNow call.
class DBusUpdateCheckClient : public UpdateCheckClient {
  Q_OBJECT

 public:
  static constexpr const char* kServiceName = "org.holonight.Packages1";
  static constexpr const char* kObjectPath = "/org/holonight/Packages1";
  static constexpr const char* kInterfaceName = "org.holonight.Packages1.UpdateCheck";
  static constexpr int kCallTimeoutMs = 30000;

  explicit DBusUpdateCheckClient(QDBusConnection connection, QObject* parent = nullptr);

  void checkNow() override;
  [[nodiscard]] const UpdateCheckClientStatus& status() const override { return status_; }

  // The CheckNow call, with bus activation enabled.
  [[nodiscard]] static QDBusMessage checkNowMessage();

 private slots:
  void onPropertiesChanged(const QString& interface_name, const QVariantMap& changed_properties,
                           const QStringList& invalidated);
  void onServiceStatusChanged();

 private:
  void onServiceAppeared();
  void onServiceGone();
  void fetchAll();
  void apply(const QVariantMap& properties);
  void setStatus(const UpdateCheckClientStatus& status);

  QDBusConnection connection_;
  QDBusServiceWatcher watcher_;
  UpdateCheckClientStatus status_;
};
