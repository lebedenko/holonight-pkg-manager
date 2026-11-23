#include "DBusTransactionService.h"

#include "DBusOwnerTracker.h"
#include "HelperWire.h"

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrentRun>

#include <utility>

namespace {

using holonight_packages_domain::CancelReply;
using holonight_packages_domain::ExecuteReply;
using holonight_packages_domain::PrepareReply;
using holonight_packages_domain::ServiceError;

QDBusMessage busDaemonCall(const QString& method) {
  auto message =
      QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                     QStringLiteral("org.freedesktop.DBus"), method);
  message << QString::fromLatin1(DBusTransactionService::kServiceName);
  return message;
}

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
  QObject::connect(&connection_watcher_, &QFutureWatcher<QDBusConnection>::finished, this,
                   &DBusTransactionService::onConnected);
}

DBusTransactionService::~DBusTransactionService() = default;

void DBusTransactionService::setProgressHandler(ProgressHandler handler) { progress_ = std::move(handler); }

QString DBusTransactionService::progressMatchRule() {
  return QStringLiteral("type='signal',sender='%1',path='%2',interface='%3',member='Progress'")
      .arg(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kInterfaceName));
}

void DBusTransactionService::setCapabilityInvalidatedHandler(const std::function<void()>& handler) {
  capability_invalidated_ = handler;
}

void DBusTransactionService::withConnection(std::function<void()> operation) {
  if (bus_) {
    operation();
    return;
  }
  pending_operations_.push_back(std::move(operation));
  if (pending_operations_.size() == 1) {
    connection_watcher_.setFuture(
        QtConcurrent::run([factory = connect_] { return factory ? factory() : QDBusConnection(QString()); }));
  }
}

void DBusTransactionService::onConnected() {
  auto connection = connection_watcher_.future().takeResult();
  if (connection.isConnected()) {
    connection.connect(QLatin1String(kServiceName), QLatin1String(kObjectPath), QLatin1String(kInterfaceName),
                       QStringLiteral("Progress"), this,
                       SLOT(onProgress(QString, QString, uint, uint, QString, QDBusMessage)));
    bus_ = connection;
    owner_tracker_ = std::make_unique<DBusOwnerTracker>(connection, QLatin1String(kServiceName));
    connect(owner_tracker_.get(), &DBusOwnerTracker::changed, this, [this] {
      if (capability_invalidated_) {
        capability_invalidated_();
      }
    });
  }
  for (auto& operation : std::exchange(pending_operations_, {})) {
    operation();
  }
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
  withConnection([this, message, timeout_ms, done = std::move(done), decode = std::move(decode)] mutable {
    if (!bus_) {
      postUnavailable<Reply>(std::move(done));
      return;
    }
    auto watcher = std::make_unique<QDBusPendingCallWatcher>(bus_->asyncCall(message, timeout_ms), this);
    connect(watcher.get(), &QDBusPendingCallWatcher::finished, this,
            [done = std::move(done), decode = std::move(decode)](QDBusPendingCallWatcher* finished) {
              const QDBusMessage reply = finished->reply();
              finished->deleteLater();
              done(decode(reply));
            });
    static_cast<void>(watcher.release());  // QObject parent owns the pending call.
  });
}

void DBusTransactionService::preparePlan(const holonight_packages_domain::PlanRequest& request, PrepareDone done) {
  if (request.action == holonight_packages_domain::PlanAction::Upgrade) {
    const auto owner = std::exchange(upgrade_owner_, std::nullopt);
    if (!owner || !owner_tracker_ || owner->generation != owner_tracker_->generation() ||
        (!owner_tracker_->owner().isEmpty() && owner->name != owner_tracker_->owner())) {
      postUnavailable<PrepareReply>(std::move(done));
      return;
    }
    const auto confirm_owner = busDaemonCall(QStringLiteral("GetNameOwner"));
    auto* call = new QDBusPendingCallWatcher(bus_->asyncCall(confirm_owner, timeouts_.probe_ms), this);
    connect(call, &QDBusPendingCallWatcher::finished, this,
            [this, owner = *owner, done = std::move(done)](QDBusPendingCallWatcher* finished) mutable {
              const QDBusPendingReply<QString> reply = *finished;
              finished->deleteLater();
              if (!reply.isValid() || reply.value() != owner.name || owner_tracker_->generation() != owner.generation) {
                done(std::unexpected(unavailable()));
                return;
              }
              auto message =
                  QDBusMessage::createMethodCall(owner.name, QLatin1String(kObjectPath), QLatin1String(kInterfaceName),
                                                 QStringLiteral("PrepareUpgrade"));
              message.setAutoStartService(false);
              message << QVariantMap{};
              active_owner_ = owner.name;
              active_plan_id_.clear();
              send<PrepareReply>(message, timeouts_.upgrade_prepare_ms, std::move(done),
                                 [this](const QDBusMessage& response) {
                                   PrepareReply decoded = helper_wire::decodePrepareUpgradeReply(response);
                                   if (decoded) {
                                     active_plan_id_ = decoded->plan_id;
                                   }
                                   return decoded;
                                 });
            });
    return;
  }
  active_owner_.clear();
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
  QDBusMessage message = active_owner_.isEmpty()
                             ? call(QStringLiteral("Execute"))
                             : QDBusMessage::createMethodCall(active_owner_, QLatin1String(kObjectPath),
                                                              QLatin1String(kInterfaceName), QStringLiteral("Execute"));
  message << QString::fromStdString(plan_id);
  send<ExecuteReply>(message, timeouts_.execute_ms, std::move(done),
                     [](const QDBusMessage& reply) { return helper_wire::decodeExecuteReply(reply); });
}

void DBusTransactionService::cancel(const std::string& plan_id, CancelDone done) {
  QDBusMessage message = active_owner_.isEmpty()
                             ? call(QStringLiteral("Cancel"))
                             : QDBusMessage::createMethodCall(active_owner_, QLatin1String(kObjectPath),
                                                              QLatin1String(kInterfaceName), QStringLiteral("Cancel"));
  message << QString::fromStdString(plan_id);
  send<CancelReply>(message, timeouts_.cancel_ms, std::move(done),
                    [](const QDBusMessage& reply) { return helper_wire::decodeCancelReply(reply); });
}

QString DBusTransactionService::runningBuildId(const QDBusConnection& connection) {
  auto message =
      QDBusMessage::createMethodCall(QLatin1String(kServiceName), QLatin1String(kObjectPath),
                                     QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
  message.setAutoStartService(false);
  message << QLatin1String(kInterfaceName) << QStringLiteral("BuildId");
  const auto reply = connection.call(message, QDBus::Block, 3000);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1) {
    return {};
  }
  return reply.arguments().front().value<QDBusVariant>().variant().toString();
}

void DBusTransactionService::queryProtocolVersion(holonight_packages_domain::ProbeMode mode, VersionDone done) {
  using holonight_packages_domain::ProbeMode;
  withConnection([this, mode, done = std::move(done)] mutable {
    if (mode == ProbeMode::AutoStart) {
      upgrade_owner_.reset();
    }
    findVersionOwner(mode, std::move(done), mode == ProbeMode::AutoStart);
  });
}

void DBusTransactionService::findVersionOwner(holonight_packages_domain::ProbeMode mode, VersionDone done,
                                              bool may_activate) {
  using holonight_packages_domain::VersionReply;
  if (!bus_) {
    postUnavailable<VersionReply>(std::move(done));
    return;
  }
  const auto request = busDaemonCall(QStringLiteral("GetNameOwner"));
  auto* call = new QDBusPendingCallWatcher(bus_->asyncCall(request, timeouts_.probe_ms), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, mode, done = std::move(done), may_activate](QDBusPendingCallWatcher* finished) mutable {
            const QDBusPendingReply<QString> reply = *finished;
            finished->deleteLater();
            if (reply.isValid()) {
              readVersion(reply.value(), mode, std::move(done));
              return;
            }
            if (!may_activate || reply.error().name() != QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner")) {
              done(std::unexpected(unavailable()));
              return;
            }
            auto activate = busDaemonCall(QStringLiteral("StartServiceByName"));
            activate << uint{0};
            auto* started = new QDBusPendingCallWatcher(bus_->asyncCall(activate, timeouts_.probe_ms), this);
            connect(started, &QDBusPendingCallWatcher::finished, this,
                    [this, mode, done = std::move(done)](QDBusPendingCallWatcher* finished_activation) mutable {
                      const QDBusPendingReply<uint> reply = *finished_activation;
                      finished_activation->deleteLater();
                      if (reply.isValid()) {
                        findVersionOwner(mode, std::move(done), false);
                      } else {
                        done(std::unexpected(unavailable()));
                      }
                    });
          });
}

void DBusTransactionService::readVersion(const QString& owner, holonight_packages_domain::ProbeMode mode,
                                         VersionDone done) {
  using holonight_packages_domain::ProbeMode;
  using holonight_packages_domain::VersionReply;
  const auto generation = owner_tracker_->generation();
  auto message = QDBusMessage::createMethodCall(
      owner, QLatin1String(kObjectPath), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
  message << QLatin1String(kInterfaceName) << QStringLiteral("ProtocolVersion");
  message.setAutoStartService(false);
  send<VersionReply>(message, timeouts_.probe_ms, std::move(done),
                     [this, mode, owner, generation](const QDBusMessage& reply) -> VersionReply {
                       const auto current = owner_tracker_->owner();
                       const auto current_generation = owner_tracker_->generation();
                       if (current_generation != generation || (!current.isEmpty() && current != owner)) {
                         return std::unexpected(unavailable());
                       }
                       auto version = helper_wire::decodeProtocolVersionReply(reply);
                       if (mode == ProbeMode::AutoStart && version && *version >= 2) {
                         // The request targets a unique name. Local QtDBus dispatch can omit the sender on the reply.
                         upgrade_owner_ = UpgradeOwner{.name = owner, .generation = current_generation};
                       }
                       return version;
                     });
}

void DBusTransactionService::onProgress(const QString& plan_id, const QString& phase, uint current, uint total,
                                        const QString& item, const QDBusMessage& message) {
  if ((!active_owner_.isEmpty() && active_owner_ != message.service()) || !progress_ ||
      plan_id.toStdString() != active_plan_id_) {
    return;
  }
  if (const auto update = helper_wire::decodeProgress(phase, current, total, item)) {
    progress_(active_plan_id_, *update);
  }
}
