#include "DBusUpdateSubscription.h"

#include "DBusOwnerTracker.h"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <memory>
#include <utility>

namespace {
constexpr auto kProperties = "org.freedesktop.DBus.Properties";
}

DBusUpdateSubscription::DBusUpdateSubscription(const DBusOwnerTracker& tracker, QString path, QString interface,
                                               Apply apply, std::function<void()> completed)
    : tracker_(tracker),
      generation_(tracker.generation()),
      owner_(tracker.owner()),
      path_(std::move(path)),
      interface_(std::move(interface)),
      apply_(std::move(apply)),
      completed_(std::move(completed)) {
  auto connection = tracker_.connection();
  connection.connect(owner_, path_, QLatin1String(kProperties), QStringLiteral("PropertiesChanged"), this,
                     SLOT(onPropertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
  if (completed_) {
    connection.connect(owner_, path_, interface_, QStringLiteral("StatusChanged"), this,
                       SLOT(onStatusChanged(QDBusMessage)));
  }

  auto request = QDBusMessage::createMethodCall(owner_, path_, QLatin1String(kProperties), QStringLiteral("GetAll"));
  request.setAutoStartService(false);
  request << interface_;
  auto call = std::make_unique<QDBusPendingCallWatcher>(connection.asyncCall(request), this);
  connect(call.get(), &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* finished) {
    const QDBusPendingReply<QVariantMap> reply = *finished;
    finished->deleteLater();
    if (tracker_.generation() != generation_) {
      return;
    }
    initializing_ = false;
    if (reply.isValid() || !initial_changes_.isEmpty()) {
      auto properties = reply.isValid() ? reply.value() : QVariantMap{};
      for (auto it = initial_changes_.cbegin(); it != initial_changes_.cend(); ++it) {
        properties.insert(it.key(), it.value());
      }
      initial_changes_.clear();
      apply_(properties);
    }
  });
  static_cast<void>(call.release());  // QObject parent owns the pending call.
}

DBusUpdateSubscription::~DBusUpdateSubscription() {
  auto connection = tracker_.connection();
  connection.disconnect(owner_, path_, QLatin1String(kProperties), QStringLiteral("PropertiesChanged"), this,
                        SLOT(onPropertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
  if (completed_) {
    connection.disconnect(owner_, path_, interface_, QStringLiteral("StatusChanged"), this,
                          SLOT(onStatusChanged(QDBusMessage)));
  }
}

void DBusUpdateSubscription::onPropertiesChanged(const QString& interface, const QVariantMap& changes,
                                                 const QStringList& /*invalidated*/, const QDBusMessage& message) {
  if (tracker_.generation() != generation_ || message.service() != owner_ || interface != interface_) {
    return;
  }
  if (!initializing_) {
    apply_(changes);
    return;
  }
  for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
    initial_changes_.insert(it.key(), it.value());
  }
}

void DBusUpdateSubscription::onStatusChanged(const QDBusMessage& message) {
  if (tracker_.generation() == generation_ && message.service() == owner_ && completed_) {
    completed_();
  }
}
