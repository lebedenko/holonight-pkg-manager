#include "PolkitAuthorizer.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace holonight_helper {
namespace {

constexpr auto kService = "org.freedesktop.PolicyKit1";
constexpr auto kPath = "/org/freedesktop/PolicyKit1/Authority";
constexpr auto kInterface = "org.freedesktop.PolicyKit1.Authority";
constexpr uint kAllowUserInteraction = 1;

using holonight_packages_application::AuthorizationDecision;

}  // namespace

QDBusArgument& operator<<(QDBusArgument& argument, const PolkitSubject& value) {
  argument.beginStructure();
  argument << value.kind << value.details;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, PolkitSubject& value) {
  argument.beginStructure();
  argument >> value.kind >> value.details;
  argument.endStructure();
  return argument;
}
QDBusArgument& operator<<(QDBusArgument& argument, const PolkitResult& value) {
  argument.beginStructure();
  argument << value.is_authorized << value.is_challenge << value.details;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, PolkitResult& value) {
  argument.beginStructure();
  argument >> value.is_authorized >> value.is_challenge >> value.details;
  argument.endStructure();
  return argument;
}

void registerPolkitTypes() {
  static const bool registered = [] {
    qDBusRegisterMetaType<PolkitSubject>();
    qDBusRegisterMetaType<PolkitResult>();
    qDBusRegisterMetaType<QMap<QString, QString>>();
    return true;
  }();
  static_cast<void>(registered);
}

PolkitAuthorizer::PolkitAuthorizer(QDBusConnection connection, int callTimeoutMs, QObject* parent)
    : QObject(parent), connection_(std::move(connection)), callTimeoutMs_(callTimeoutMs) {
  registerPolkitTypes();
}

void PolkitAuthorizer::check(const holonight_packages_application::AuthorizationRequest& request,
                             std::function<void(AuthorizationDecision)> done) {
  // The subject is the caller's unique bus name; there is no process-ID field in the request to put in its place.
  PolkitSubject subject{
      .kind = QStringLiteral("system-bus-name"),
      .details = {{QStringLiteral("name"), QString::fromStdString(request.bus_name)}},
  };
  QMap<QString, QString> details;
  for (const auto& [key, value] : request.details) {
    details.insert(QString::fromStdString(key), QString::fromStdString(value));
  }
  const uint flags = request.allow_user_interaction ? kAllowUserInteraction : 0;
  const QString cancellation_id = QString::fromStdString(request.cancellation_id);

  QDBusMessage call =
      QDBusMessage::createMethodCall(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                                     QString::fromLatin1(kInterface), QStringLiteral("CheckAuthorization"));
  call << QVariant::fromValue(subject) << QString::fromStdString(request.action_id) << QVariant::fromValue(details)
       << flags << cancellation_id;
  auto* watcher = new QDBusPendingCallWatcher(connection_.asyncCall(call, callTimeoutMs_), this);
  pending_.insert(cancellation_id, watcher);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, cancellation_id, done = std::move(done)](QDBusPendingCallWatcher* finished) {
            pending_.remove(cancellation_id);
            finished->deleteLater();
            const QDBusPendingReply<PolkitResult> reply = *finished;
            if (reply.isError()) {
              done(AuthorizationDecision::Error);
              return;
            }
            const PolkitResult result = reply.value();
            if (result.is_authorized) {
              done(AuthorizationDecision::Authorized);
            } else if (result.is_challenge) {
              done(AuthorizationDecision::Challenge);
            } else {
              done(AuthorizationDecision::NotAuthorized);
            }
          });
}

void PolkitAuthorizer::cancel(const std::string& cancellation_id) {
  const QString plan = QString::fromStdString(cancellation_id);
  if (auto* watcher = pending_.take(plan)) {
    watcher->disconnect(this);  // the answer, if it still comes, is ignored
    watcher->deleteLater();
  }
  QDBusMessage call =
      QDBusMessage::createMethodCall(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                                     QString::fromLatin1(kInterface), QStringLiteral("CancelCheckAuthorization"));
  call << plan;
  connection_.asyncCall(call);
}

}  // namespace holonight_helper
