#include "UpdateStatusService.h"

#include "UpdatesAdaptor.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QVariantMap>

UpdateStatusService::UpdateStatusService(holonight_packages_application::UpdateMonitor* monitor, QObject* parent)
    : QObject(parent), monitor_(monitor), connection_(QStringLiteral("")) {
  new UpdatesAdaptor(this);
  connect(monitor_, &holonight_packages_application::UpdateMonitor::statusChanged, this,
          [this] { emitPropertiesChanged(); });
}

bool UpdateStatusService::registerOn(const QDBusConnection& connection) {
  connection_ = connection;
  if (!connection_.registerObject(QString::fromLatin1(kObjectPath), this, QDBusConnection::ExportAdaptors)) {
    error_ = QStringLiteral("cannot register object %1").arg(QString::fromLatin1(kObjectPath));
    return false;
  }
  if (!connection_.registerService(QString::fromLatin1(kServiceName))) {
    connection_.unregisterObject(QString::fromLatin1(kObjectPath));
    error_ =
        QStringLiteral("cannot own %1: %2").arg(QString::fromLatin1(kServiceName), connection_.lastError().message());
    return false;
  }
  return true;
}

void UpdateStatusService::emitPropertiesChanged() {
  if (!connection_.isConnected()) {
    return;
  }
  const UpdatesAdaptor* adaptor = findChild<UpdatesAdaptor*>();
  QVariantMap changed{
      {QStringLiteral("State"), adaptor->state()},
      {QStringLiteral("Count"), adaptor->count()},
      {QStringLiteral("IgnoredCount"), adaptor->ignoredCount()},
      {QStringLiteral("DownloadSizeBytes"), adaptor->downloadSizeBytes()},
      {QStringLiteral("DataAsOf"), adaptor->dataAsOf()},
      {QStringLiteral("LastError"), adaptor->lastError()},
  };
  QDBusMessage signal =
      QDBusMessage::createSignal(QString::fromLatin1(kObjectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
                                 QStringLiteral("PropertiesChanged"));
  signal << QString::fromLatin1(kInterfaceName) << changed << QStringList{};
  connection_.send(signal);
}
