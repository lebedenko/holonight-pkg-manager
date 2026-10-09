#include "DBusUpdateCheckClient.h"

#include <QDBusConnectionInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

DBusUpdateCheckClient::DBusUpdateCheckClient(QDBusConnection connection, QObject* parent)
    : UpdateCheckClient(parent),
      connection_(std::move(connection)),
      watcher_(QString::fromLatin1(kServiceName), connection_, QDBusServiceWatcher::WatchForOwnerChange) {
  connect(&watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this] { onServiceAppeared(); });
  connect(&watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this] { onServiceGone(); });
  if (connection_.isConnected() && connection_.interface() != nullptr &&
      connection_.interface()->isServiceRegistered(QString::fromLatin1(kServiceName))) {
    onServiceAppeared();
  }
}

QDBusMessage DBusUpdateCheckClient::checkNowMessage() {
  QDBusMessage message =
      QDBusMessage::createMethodCall(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                                     QString::fromLatin1(kInterfaceName), QStringLiteral("CheckNow"));
  message.setAutoStartService(true);
  return message;
}

void DBusUpdateCheckClient::checkNow() {
  if (!connection_.isConnected()) {
    emit checkNowFailed();
    return;
  }
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(checkNowMessage(), kCallTimeoutMs), this);
  connect(call, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* finished) {
    const QDBusPendingReply<> reply = *finished;
    finished->deleteLater();
    if (reply.isError()) {
      emit checkNowFailed();
    } else {
      emit checkNowSucceeded();
    }
  });
}

void DBusUpdateCheckClient::onServiceAppeared() {
  connection_.connect(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                      SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
  connection_.connect(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                      QString::fromLatin1(kInterfaceName), QStringLiteral("StatusChanged"), this,
                      SLOT(onServiceStatusChanged()));
  UpdateCheckClientStatus updated = status_;
  updated.serviceReachable = true;
  setStatus(updated);
  fetchAll();
}

void DBusUpdateCheckClient::onServiceGone() {
  connection_.disconnect(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                         QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                         SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
  connection_.disconnect(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                         QString::fromLatin1(kInterfaceName), QStringLiteral("StatusChanged"), this,
                         SLOT(onServiceStatusChanged()));
  // Nothing is checking any more; what the service last reported stays as history.
  UpdateCheckClientStatus updated = status_;
  updated.serviceReachable = false;
  updated.checking = false;
  setStatus(updated);
}

void DBusUpdateCheckClient::fetchAll() {
  QDBusMessage request =
      QDBusMessage::createMethodCall(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                                     QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
  request << QString::fromLatin1(kInterfaceName);
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(request), this);
  connect(call, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* finished) {
    const QDBusPendingReply<QVariantMap> reply = *finished;
    finished->deleteLater();
    if (reply.isValid()) {
      apply(reply.value());
    }
  });
}

void DBusUpdateCheckClient::onPropertiesChanged(const QString& interface_name, const QVariantMap& changed_properties,
                                                const QStringList& /*invalidated*/) {
  if (interface_name == QString::fromLatin1(kInterfaceName)) {
    apply(changed_properties);
  }
}

void DBusUpdateCheckClient::onServiceStatusChanged() { emit checkCompleted(); }

void DBusUpdateCheckClient::apply(const QVariantMap& properties) {
  UpdateCheckClientStatus updated = status_;
  updated.serviceReachable = true;
  updated.canCheck = properties.value(QStringLiteral("CanCheck"), updated.canCheck).toBool();
  updated.checking = properties.value(QStringLiteral("Checking"), updated.checking).toBool();
  updated.lastCheckTime = properties.value(QStringLiteral("LastCheckTime"), updated.lastCheckTime).toLongLong();
  updated.lastCheckSucceeded =
      properties.value(QStringLiteral("LastCheckSucceeded"), updated.lastCheckSucceeded).toBool();
  updated.lastCheckError = properties.value(QStringLiteral("LastCheckError"), updated.lastCheckError).toString();
  updated.snapshotFetchedAt =
      properties.value(QStringLiteral("SnapshotFetchedAt"), updated.snapshotFetchedAt).toLongLong();
  setStatus(updated);
}

void DBusUpdateCheckClient::setStatus(const UpdateCheckClientStatus& status) {
  if (status == status_) {
    return;
  }
  status_ = status;
  emit statusChanged();
}
