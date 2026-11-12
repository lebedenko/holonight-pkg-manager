#include "HelperAdaptor.h"

#include "HelperService.h"

namespace holonight_helper {

HelperAdaptor::HelperAdaptor(HelperService* service) : QDBusAbstractAdaptor(service), service_(service) {}

bool HelperAdaptor::busy() const { return service_->busy(); }

void HelperAdaptor::PreparePlan(const QString& action, const QStringList& names, const QVariantMap& options,
                                const QDBusMessage& message) {
  message.setDelayedReply(true);
  service_->preparePlan(message, action, names, options);
}

void HelperAdaptor::PrepareUpgrade(const QVariantMap& options, const QDBusMessage& message) {
  message.setDelayedReply(true);
  service_->prepareUpgrade(message, options);
}

void HelperAdaptor::Execute(const QString& planId, const QDBusMessage& message) {
  message.setDelayedReply(true);
  service_->execute(message, planId);
}

void HelperAdaptor::Cancel(const QString& planId, const QDBusMessage& message) {
  message.setDelayedReply(true);
  service_->cancel(message, planId);
}

}  // namespace holonight_helper
