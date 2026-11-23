#include "UpdateStatusClient.h"

#include "DBusOwnerTracker.h"
#include "DBusUpdateSubscription.h"

#include <utility>

UpdateStatusClient::UpdateStatusClient(QDBusConnection connection, QObject* parent)
    : UpdateStatusClient(std::make_shared<DBusOwnerTracker>(std::move(connection), QLatin1String(kServiceName)),
                         parent) {}

UpdateStatusClient::UpdateStatusClient(std::shared_ptr<DBusOwnerTracker> tracker, QObject* parent)
    : UpdateStatusPresentation(parent), tracker_(std::move(tracker)) {
  connect(tracker_.get(), &DBusOwnerTracker::changed, this, &UpdateStatusClient::onOwnerChanged);
  onOwnerChanged();
}

UpdateStatusClient::~UpdateStatusClient() = default;

void UpdateStatusClient::onOwnerChanged() {
  subscription_.reset();
  setValues(false, 0, {});
  if (!tracker_->owner().isEmpty()) {
    subscription_ =
        std::make_unique<DBusUpdateSubscription>(*tracker_, QLatin1String(kObjectPath), QLatin1String(kInterfaceName),
                                                 [this](const QVariantMap& properties) { apply(properties); });
  }
}

void UpdateStatusClient::apply(const QVariantMap& properties) {
  setValues(true, properties.value(QStringLiteral("Count"), count()).toInt(),
            properties.value(QStringLiteral("State"), state()).toString());
}
