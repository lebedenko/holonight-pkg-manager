#include "DBusTransactionService.h"

#include "HelperWire.h"

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QTimer>
#include <QVariantMap>

#include <utility>

namespace {

using holonight_packages_domain::CancelReply;
using holonight_packages_domain::ExecuteReply;
using holonight_packages_domain::PrepareReply;
using holonight_packages_domain::ServiceError;

QStringList toQStringList(const std::vector<std::string>& names) {
  QStringList list;
  list.reserve(static_cast<qsizetype>(names.size()));
  for (const std::string& name : names) {
    list.push_back(QString::fromStdString(name));
  }
  return list;
}

ServiceError unavailable() { return ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}}; }

}  // namespace

DBusTransactionService::DBusTransactionService(ConnectionFactory connect, QObject* parent)
    : DBusTransactionService(std::move(connect), Timeouts{}, parent) {}

DBusTransactionService::DBusTransactionService(ConnectionFactory connect, Timeouts timeouts, QObject* parent)
    : QObject(parent), connect_(std::move(connect)), timeouts_(timeouts) {
  helper_wire::registerHelperWireTypes();
}

DBusTransactionService::~DBusTransactionService() = default;

void DBusTransactionService::setProgressHandler(ProgressHandler handler) { progress_ = std::move(handler); }

QString DBusTransactionService::progressMatchRule() {
  return QStringLiteral("type='signal',sender='%1',path='%2',interface='%3',member='Progress'")
      .arg(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kInterfaceName));
}

std::optional<QDBusConnection> DBusTransactionService::ensureConnection() {
  if (!bus_) {
    if (!connect_) {
      return std::nullopt;
    }
    QDBusConnection connection = connect_();
    if (!connection.isConnected()) {
      return std::nullopt;  // not cached: a later call retries the factory
    }
    // The subscription is installed before any Execute is sent, so the first Resolving event cannot be missed.
    connection.connect(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kInterfaceName),
                       QStringLiteral("Progress"), this, SLOT(onProgress(QString, QString, uint, uint, QString)));
    bus_ = connection;
  }
  return bus_;
}

QDBusMessage DBusTransactionService::call(const QString& method) {
  return QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath),
                                        QLatin1String(kInterfaceName), method);
}

template <typename Reply, typename Done>
void DBusTransactionService::postUnavailable(Done done) {
  // Never call back from inside the call that started it (the port contract).
  QTimer::singleShot(0, this, [done = std::move(done)] { done(Reply(std::unexpected(unavailable()))); });
}

template <typename Reply, typename Done, typename Decode>
void DBusTransactionService::send(const QDBusMessage& message, int timeout_ms, Done done, Decode decode) {
  const auto bus = ensureConnection();
  if (!bus) {
    postUnavailable<Reply>(std::move(done));
    return;
  }
  auto* watcher = new QDBusPendingCallWatcher(bus->asyncCall(message, timeout_ms), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, done = std::move(done), decode = std::move(decode)](QDBusPendingCallWatcher* finished) {
            const QDBusMessage reply = finished->reply();
            finished->deleteLater();
            Reply decoded = decode(reply);
            done(std::move(decoded));
          });
}

void DBusTransactionService::preparePlan(const holonight_packages_domain::PlanRequest& request, PrepareDone done) {
  QDBusMessage message = call(QStringLiteral("PreparePlan"));
  const std::string action{holonight_packages_domain::planActionName(request.action)};
  // Exactly the declared arguments: an action, the names and an empty options map (REQ-C-004).
  message << QString::fromStdString(action) << toQStringList(request.names) << QVariantMap{};
  send<PrepareReply>(message, timeouts_.prepare_ms, std::move(done), [this, action](const QDBusMessage& reply) {
    PrepareReply decoded = helper_wire::decodePrepareReply(reply, action);
    if (decoded) {
      active_plan_id_ = decoded->plan_id;
    }
    return decoded;
  });
}

void DBusTransactionService::execute(const std::string& plan_id, ExecuteDone done) {
  QDBusMessage message = call(QStringLiteral("Execute"));
  message << QString::fromStdString(plan_id);
  send<ExecuteReply>(message, timeouts_.execute_ms, std::move(done),
                     [](const QDBusMessage& reply) { return helper_wire::decodeExecuteReply(reply); });
}

void DBusTransactionService::cancel(const std::string& plan_id, CancelDone done) {
  QDBusMessage message = call(QStringLiteral("Cancel"));
  message << QString::fromStdString(plan_id);
  send<CancelReply>(message, timeouts_.cancel_ms, std::move(done),
                    [](const QDBusMessage& reply) { return helper_wire::decodeCancelReply(reply); });
}

void DBusTransactionService::onProgress(const QString& plan_id, const QString& phase, uint current, uint total,
                                        const QString& item) {
  if (!progress_ || plan_id.toStdString() != active_plan_id_) {
    return;
  }
  if (const auto update = helper_wire::decodeProgress(phase, current, total, item)) {
    progress_(active_plan_id_, *update);
  }
}
