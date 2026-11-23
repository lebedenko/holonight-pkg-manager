#pragma once

#include "UpdateStatusPresentation.h"

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>

class DBusOwnerTracker;
class DBusUpdateSubscription;

// Reads the pending-update count that holonight-packaged publishes on the session bus. It never starts the service:
// when nothing owns the name, `available` is false and the UI behaves as if the client did not exist.
class UpdateStatusClient : public UpdateStatusPresentation {
  Q_OBJECT
 public:
  static constexpr const char* kServiceName = "org.holonight.Packages1";
  static constexpr const char* kObjectPath = "/org/holonight/Packages1";
  static constexpr const char* kInterfaceName = "org.holonight.Packages1.Updates";

  explicit UpdateStatusClient(QDBusConnection connection, QObject* parent = nullptr);
  explicit UpdateStatusClient(std::shared_ptr<DBusOwnerTracker> tracker, QObject* parent = nullptr);
  ~UpdateStatusClient() override;
  UpdateStatusClient(const UpdateStatusClient&) = delete;
  UpdateStatusClient(UpdateStatusClient&&) = delete;
  UpdateStatusClient& operator=(const UpdateStatusClient&) = delete;
  UpdateStatusClient& operator=(UpdateStatusClient&&) = delete;

 private:
  void onOwnerChanged();
  void apply(const QVariantMap& properties);

  std::shared_ptr<DBusOwnerTracker> tracker_;
  std::unique_ptr<DBusUpdateSubscription> subscription_;
};
