#include "HelperService.h"

#include "HelperAdaptor.h"
#include "HelperIdentity.h"
#include "WireTypes.h"

#include <QMetaObject>

namespace holonight_helper {
namespace {

std::vector<std::string> toStd(const QStringList& values) {
  std::vector<std::string> result;
  for (const QString& value : values) {
    result.push_back(value.toStdString());
  }
  return result;
}

}  // namespace

HelperService::HelperService(QDBusConnection connection, holonight_packages_application::PlanService& plans,
                             QObject* parent)
    : QObject(parent), connection_(std::move(connection)), plans_(&plans), adaptor_(new HelperAdaptor(this)) {
  registerWireTypes();
  // Called on the worker thread; the signal itself is sent from this object's thread.
  plans_->setProgressForward([this](const std::string& owner, const std::string& planId,
                                    const holonight_packages_domain::ProgressUpdate& update) {
    QMetaObject::invokeMethod(
        this,
        [this, owner = QString::fromStdString(owner), planId = QString::fromStdString(planId),
         phase = QString::fromUtf8(holonight_packages_domain::progressPhaseName(update.phase).data()),
         current = update.current, total = update.total,
         item = QString::fromStdString(update.item)] { sendProgress(owner, planId, phase, current, total, item); },
        Qt::QueuedConnection);
  });
}

HelperService::~HelperService() = default;

bool HelperService::start() {
  return connection_.registerObject(QString::fromLatin1(kObjectPath), this, QDBusConnection::ExportAdaptors) &&
         connection_.registerService(QString::fromLatin1(kBusName));
}

bool HelperService::busy() const { return plans_->slotBusy(); }

void HelperService::reply(const QDBusMessage& message, const QVariantList& values) {
  connection_.send(message.createReply(values));
}

void HelperService::watch(const QString& unique_name) {
  if (watchers_.contains(unique_name)) {
    return;
  }
  auto* watcher = new QDBusServiceWatcher(unique_name, connection_, QDBusServiceWatcher::WatchForUnregistration, this);
  watchers_.insert(unique_name, watcher);
  connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, unique_name](const QString&) {
    if (auto* gone = watchers_.take(unique_name)) {
      gone->deleteLater();
    }
    plans_->callerGone(unique_name.toStdString());
  });
}

void HelperService::preparePlan(const QDBusMessage& message, const QString& action, const QStringList& names,
                                const QVariantMap& options) {
  const QString caller = message.service();
  plans_->prepare(
      caller.toStdString(), action.toStdString(), toStd(names), toStd(options.keys()),
      [this, message, caller](std::expected<holonight_packages_application::PlanService::Prepared,
                                            holonight_packages_domain::TransactionFailure>
                                  result) {
        if (!result) {
          reply(message, {QVariant::fromValue(toWire(result.error())), QString(), QVariant::fromValue(PreviewWire{})});
          return;
        }
        watch(caller);
        reply(message, {
                           QVariant::fromValue(okStatus()),
                           QString::fromStdString(result->id),
                           QVariant::fromValue(toWire(result->plan, result->expires_at)),
                       });
      });
}

void HelperService::execute(const QDBusMessage& message, const QString& planId) {
  plans_->execute(message.service().toStdString(), planId.toStdString(),
                  [this, message](const holonight_packages_domain::ExecutionResult& result) {
                    reply(message, {QVariant::fromValue(toWire(result))});
                  });
}

void HelperService::cancel(const QDBusMessage& message, const QString& planId) {
  const auto result = plans_->cancel(message.service().toStdString(), planId.toStdString());
  reply(message, {QVariant::fromValue(result ? okStatus() : toWire(result.error()))});
}

void HelperService::sendProgress(const QString& owner, const QString& planId, const QString& phase, uint current,
                                 uint total, const QString& item) {
  // A targeted signal: only the owner's unique name receives it (REQ-F-050).
  QDBusMessage signal = QDBusMessage::createTargetedSignal(
      owner, QString::fromLatin1(kObjectPath), QString::fromLatin1(kInterfaceName), QStringLiteral("Progress"));
  signal << planId << phase << current << total << item;
  connection_.send(signal);
}

}  // namespace holonight_helper
