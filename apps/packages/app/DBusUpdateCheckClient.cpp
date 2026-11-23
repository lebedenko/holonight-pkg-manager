#include "DBusUpdateCheckClient.h"

#include "DBusOwnerTracker.h"
#include "DBusUpdateSubscription.h"

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <utility>

DBusUpdateCheckClient::DBusUpdateCheckClient(QDBusConnection connection, QObject* parent)
    : DBusUpdateCheckClient(std::make_shared<DBusOwnerTracker>(std::move(connection), QLatin1String(kServiceName)),
                            parent) {}

DBusUpdateCheckClient::DBusUpdateCheckClient(std::shared_ptr<DBusOwnerTracker> tracker, QObject* parent)
    : UpdateCheckClient(parent), tracker_(std::move(tracker)) {
  connect(tracker_.get(), &DBusOwnerTracker::changed, this, &DBusUpdateCheckClient::onOwnerChanged);
  onOwnerChanged();
}

DBusUpdateCheckClient::~DBusUpdateCheckClient() = default;

QDBusMessage DBusUpdateCheckClient::checkNowMessage() {
  QDBusMessage message =
      QDBusMessage::createMethodCall(QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
                                     QString::fromLatin1(kInterfaceName), QStringLiteral("CheckNow"));
  message.setAutoStartService(true);
  return message;
}

void DBusUpdateCheckClient::checkNow() {
  const auto& connection = tracker_->connection();
  if (!connection.isConnected()) {
    emit checkNowFailed();
    return;
  }
  auto* call = new QDBusPendingCallWatcher(connection.asyncCall(checkNowMessage(), kCallTimeoutMs), this);
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

void DBusUpdateCheckClient::onOwnerChanged() {
  if (subscription_) {
    subscription_.reset();
    // Keep the last completed check as history.
    auto updated = status_;
    updated.serviceReachable = false;
    updated.checking = false;
    updated.canCheck = UpdateCheckClientStatus{}.canCheck;
    setStatus(updated);
  }
  if (!tracker_->owner().isEmpty()) {
    subscription_ = std::make_unique<DBusUpdateSubscription>(
        *tracker_, QLatin1String(kObjectPath), QLatin1String(kInterfaceName),
        [this](const QVariantMap& properties) { apply(properties); }, [this] { emit checkCompleted(); });
    auto updated = status_;
    updated.serviceReachable = true;
    setStatus(updated);
  }
}

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
