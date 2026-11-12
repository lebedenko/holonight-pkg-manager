#pragma once

#include "HelperIdentity.h"
#include "HelperIntrospection.h"

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace holonight_helper {

class HelperService;

// Thin mapping from dbus/org.holonight.PackageHelper1.xml to HelperService. The interface exposes no method that takes
// a command, a path or a resolved package list (REQ-F-017, REQ-F-018). Replies are produced by the service, so the
// introspection comes from the XML and not from the C++ signatures.
class HelperAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.holonight.PackageHelper1.Transactions")
  Q_CLASSINFO("D-Bus Introspection", HOLONIGHT_HELPER_INTROSPECTION)
  Q_PROPERTY(uint ProtocolVersion READ protocolVersion)
  Q_PROPERTY(bool Busy READ busy)

 public:
  explicit HelperAdaptor(HelperService* service);

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static): a Q_PROPERTY READ must be a member.
  [[nodiscard]] uint protocolVersion() const { return kProtocolVersion; }
  [[nodiscard]] bool busy() const;

 public slots:
  // The slot names are the D-Bus method names.
  // NOLINTBEGIN(readability-identifier-naming)
  void PreparePlan(const QString& action, const QStringList& names, const QVariantMap& options,
                   const QDBusMessage& message);
  void PrepareUpgrade(const QVariantMap& options, const QDBusMessage& message);
  void Execute(const QString& planId, const QDBusMessage& message);
  void Cancel(const QString& planId, const QDBusMessage& message);
  // NOLINTEND(readability-identifier-naming)

 private:
  HelperService* service_;
};

}  // namespace holonight_helper
