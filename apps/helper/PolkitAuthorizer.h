#pragma once

#include "holonight_packages_application/authorizer.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusPendingCallWatcher;

namespace holonight_helper {

// polkit's wire types: the subject (sa{sv}) and the result (bba{ss}) of org.freedesktop.PolicyKit1.Authority.
struct PolkitSubject {
  QString kind;
  QVariantMap details;
};
struct PolkitResult {
  bool is_authorized = false;
  bool is_challenge = false;
  QMap<QString, QString> details;
};
QDBusArgument& operator<<(QDBusArgument& argument, const PolkitSubject& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, PolkitSubject& value);
QDBusArgument& operator<<(QDBusArgument& argument, const PolkitResult& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, PolkitResult& value);
void registerPolkitTypes();

// The production Authorizer (REQ-F-065, REQ-F-066). It asks polkit about the caller's unique bus name, never a process
// ID, with user interaction allowed, and treats everything except an explicit "authorized" as a refusal.
class PolkitAuthorizer final : public QObject, public holonight_packages_application::Authorizer {
  Q_OBJECT

 public:
  // `callTimeoutMs` bounds the wait for polkit, which includes the time the user needs to type a password.
  explicit PolkitAuthorizer(QDBusConnection connection, int callTimeoutMs = 10 * 60 * 1000, QObject* parent = nullptr);

  void check(const holonight_packages_application::AuthorizationRequest& request,
             std::function<void(holonight_packages_application::AuthorizationDecision)> done) override;
  void cancel(const std::string& cancellation_id) override;

 private:
  QDBusConnection connection_;
  int callTimeoutMs_;
  QHash<QString, QDBusPendingCallWatcher*> pending_;
};

}  // namespace holonight_helper

Q_DECLARE_METATYPE(holonight_helper::PolkitSubject)
Q_DECLARE_METATYPE(holonight_helper::PolkitResult)
