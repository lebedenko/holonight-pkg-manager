#include "UpdateCheckAdaptor.h"

#include "holonight_packages_domain/update_checker.h"

#include <chrono>

namespace {

qlonglong epochSeconds(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

}  // namespace

UpdateCheckAdaptor::UpdateCheckAdaptor(UpdateStatusService* service)
    : QDBusAbstractAdaptor(service), service_(service) {
  setAutoRelaySignals(false);
}

bool UpdateCheckAdaptor::canCheck() const {
  return service_->checkService() != nullptr && service_->checkService()->canCheckForUpdates();
}

bool UpdateCheckAdaptor::checking() const {
  return service_->checkService() != nullptr && service_->checkService()->status().checking;
}

qlonglong UpdateCheckAdaptor::lastCheckTime() const {
  if (service_->checkService() == nullptr || !service_->checkService()->status().hasCheckResult) {
    return 0;
  }
  return epochSeconds(service_->checkService()->status().lastCheckTime);
}

bool UpdateCheckAdaptor::lastCheckSucceeded() const {
  return service_->checkService() != nullptr && service_->checkService()->status().hasCheckResult &&
         service_->checkService()->status().lastCheckSucceeded;
}

QString UpdateCheckAdaptor::lastCheckError() const {
  if (service_->checkService() == nullptr || !service_->checkService()->status().lastError.has_value()) {
    return {};
  }
  return QString::fromUtf8(
      holonight_packages_domain::updateCheckErrorName(*service_->checkService()->status().lastError));
}

qlonglong UpdateCheckAdaptor::snapshotFetchedAt() const {
  if (service_->checkService() == nullptr || !service_->checkService()->status().snapshotFetchedAt.has_value()) {
    return 0;
  }
  return epochSeconds(*service_->checkService()->status().snapshotFetchedAt);
}

void UpdateCheckAdaptor::CheckNow() { service_->requestOnDemandCheck(); }
