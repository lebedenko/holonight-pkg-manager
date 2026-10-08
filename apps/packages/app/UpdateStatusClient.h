#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Reads the pending-update count that holonight-packaged publishes on the session bus. It never starts the service:
// when nothing owns the name, `available` is false and the UI behaves as if the client did not exist.
class UpdateStatusClient : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(UpdateStatusClient)
  QML_UNCREATABLE("UpdateStatusClient is provided by the application")
  Q_PROPERTY(bool available READ available NOTIFY changed)
  // Pending updates excluding ignored packages; 0 while unavailable.
  Q_PROPERTY(int count READ count NOTIFY changed)
  // The service's State string ("ready", "error", ...); empty while unavailable.
  Q_PROPERTY(QString state READ state NOTIFY changed)

 public:
  static constexpr const char* kServiceName = "org.holonight.Packages1";
  static constexpr const char* kObjectPath = "/org/holonight/Packages1";
  static constexpr const char* kInterfaceName = "org.holonight.Packages1.Updates";

  explicit UpdateStatusClient(QDBusConnection connection, QObject* parent = nullptr);

  [[nodiscard]] bool available() const { return available_; }
  [[nodiscard]] int count() const { return count_; }
  [[nodiscard]] const QString& state() const { return state_; }

 signals:
  void changed();

 private slots:
  void onPropertiesChanged(const QString& interface_name, const QVariantMap& changed_properties,
                           const QStringList& invalidated);

 private:
  void onServiceAppeared();
  void onServiceGone();
  void fetchAll();
  void apply(const QVariantMap& properties);
  void setValues(bool available, int count, const QString& state);

  QDBusConnection connection_;
  QDBusServiceWatcher watcher_;
  bool available_ = false;
  int count_ = 0;
  QString state_;
};
