#pragma once

#include "UpdateCheckClient.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QVariantMap>

#include <memory>

class DBusOwnerTracker;
class DBusUpdateSubscription;

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
  explicit DBusUpdateCheckClient(std::shared_ptr<DBusOwnerTracker> tracker, QObject* parent = nullptr);
  ~DBusUpdateCheckClient() override;
  DBusUpdateCheckClient(const DBusUpdateCheckClient&) = delete;
  DBusUpdateCheckClient(DBusUpdateCheckClient&&) = delete;
  DBusUpdateCheckClient& operator=(const DBusUpdateCheckClient&) = delete;
  DBusUpdateCheckClient& operator=(DBusUpdateCheckClient&&) = delete;

  void checkNow() override;
  [[nodiscard]] const UpdateCheckClientStatus& status() const override { return status_; }

  // The CheckNow call, with bus activation enabled.
  [[nodiscard]] static QDBusMessage checkNowMessage();

 private:
  void onOwnerChanged();
  void apply(const QVariantMap& properties);
  void setStatus(const UpdateCheckClientStatus& status);

  std::shared_ptr<DBusOwnerTracker> tracker_;
  std::unique_ptr<DBusUpdateSubscription> subscription_;
  UpdateCheckClientStatus status_;
};
