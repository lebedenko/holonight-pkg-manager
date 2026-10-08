#include "UpdatesAdaptor.h"

using holonight_packages_application::UpdateState;

UpdatesAdaptor::UpdatesAdaptor(UpdateStatusService* service) : QDBusAbstractAdaptor(service), service_(service) {
  setAutoRelaySignals(false);
}

QString UpdatesAdaptor::state() const {
  switch (service_->status().state) {
    case UpdateState::Loading:
      return QStringLiteral("loading");
    case UpdateState::Ready:
      return QStringLiteral("ready");
    case UpdateState::NoDatabases:
      return QStringLiteral("no-databases");
    case UpdateState::Error:
      return QStringLiteral("error");
  }
  return QStringLiteral("error");
}

uint UpdatesAdaptor::count() const { return static_cast<uint>(service_->status().updateCount); }

uint UpdatesAdaptor::ignoredCount() const { return static_cast<uint>(service_->status().ignoredCount); }

qulonglong UpdatesAdaptor::downloadSizeBytes() const { return service_->status().totalDownloadBytes; }

qlonglong UpdatesAdaptor::dataAsOf() const { return service_->status().dataAsOfEpochSeconds; }

QString UpdatesAdaptor::lastError() const { return QString::fromStdString(service_->status().lastError); }

void UpdatesAdaptor::Refresh() { service_->refresh(); }
