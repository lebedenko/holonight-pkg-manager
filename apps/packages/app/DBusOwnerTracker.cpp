#include "DBusOwnerTracker.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <utility>

DBusOwnerTracker::DBusOwnerTracker(QDBusConnection connection, const QString& service)
    : connection_(std::move(connection)), watcher_(service, connection_, QDBusServiceWatcher::WatchForOwnerChange) {
  connect(&watcher_, &QDBusServiceWatcher::serviceOwnerChanged, this,
          [this](const QString&, const QString&, const QString& owner) {
            ++generation_;
            owner_ = owner;
            emit changed();
          });
  auto request =
      QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                     QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
  request << service;
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(request), this);
  connect(call, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* finished) {
    const QDBusPendingReply<QString> reply = *finished;
    finished->deleteLater();
    if (generation_ == 0 && reply.isValid()) {
      ++generation_;
      owner_ = reply.value();
      emit changed();
    }
  });
}
