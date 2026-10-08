#include "UpdateStatusClient.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

UpdateStatusClient::UpdateStatusClient(QDBusConnection connection, QObject* parent)
    : QObject(parent),
      connection_(std::move(connection)),
      watcher_(QString::fromLatin1(kServiceName), connection_, QDBusServiceWatcher::WatchForOwnerChange) {
  connect(&watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this] { onServiceAppeared(); });
  connect(&watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this] { onServiceGone(); });
  if (connection_.isConnected() && connection_.interface() != nullptr &&
      connection_.interface()->isServiceRegistered(QString::fromLatin1(kServiceName))) {
    onServiceAppeared();
  }
}

void UpdateStatusClient::onServiceAppeared() {
  connection_.connect(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                      SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
  fetchAll();
}

void UpdateStatusClient::onServiceGone() {
  connection_.disconnect(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                         QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                         SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
  setValues(false, 0, QString());
}

void UpdateStatusClient::fetchAll() {
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

void UpdateStatusClient::onPropertiesChanged(const QString& interface_name, const QVariantMap& changed_properties,
                                             const QStringList& /*invalidated*/) {
  if (interface_name == QString::fromLatin1(kInterfaceName)) {
    apply(changed_properties);
  }
}

void UpdateStatusClient::apply(const QVariantMap& properties) {
  setValues(true, properties.value(QStringLiteral("Count"), count_).toInt(),
            properties.value(QStringLiteral("State"), state_).toString());
}

void UpdateStatusClient::setValues(bool available, int count, const QString& state) {
  if (available_ == available && count_ == count && state_ == state) {
    return;
  }
  available_ = available;
  count_ = count;
  state_ = state;
  emit changed();
}
