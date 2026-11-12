#pragma once

#include "holonight_packages_domain/transaction_service.h"

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <optional>
#include <string_view>

// The GUI's own copies of the structures in apps/helper/dbus/org.holonight.PackageHelper1.xml. The helper's
// WireTypes.h is helper-internal (linking it would drag the authorization and libalpm targets into the GUI), so the
// two copies are proven to agree by tests (DESIGN section 12.2). A reply carries only enumerated codes, package
// names, versions and integers.
namespace helper_wire {

struct ChoiceWire {  // (ssas)
  QString kind;
  QString subject;
  QStringList candidates;
};

struct StatusWire {  // (sasa(ssas)): code is empty for success
  QString code;
  QStringList names;
  QList<ChoiceWire> choices;
};

struct PlanItemWire {  // (sssss)
  QString name;
  QString action;
  QString old_version;
  QString new_version;
  QString repository;
};

struct HookItemWire {  // (sb)
  QString name;
  bool conditional = false;
};

struct PreviewWire {  // (sa(sssss)a(sssss)a(sssss)a(sb)x)
  QString action;
  QList<PlanItemWire> targets;
  QList<PlanItemWire> dependencies;
  QList<PlanItemWire> orphans;
  QList<HookItemWire> hooks;
  qlonglong expires_at = 0;  // epoch seconds
};

struct ChangeWire {  // (ssss)
  QString name;
  QString action;
  QString old_version;
  QString new_version;
};

// The upgrade preview of PrepareUpgrade (system-upgrade DESIGN 6.1).
struct UpgradeItemWire {  // (ssss)
  QString name;
  QString old_version;
  QString new_version;
  QString repository;
};

struct ReplacementWire {  // (sssss)
  QString old_name;
  QString old_version;
  QString new_name;
  QString new_version;
  QString repository;
};

struct NewDependencyWire {  // (sss)
  QString name;
  QString version;
  QString repository;
};

struct HeldBackWire {  // (ss)
  QString name;
  QString installed_version;
};

struct UpgradePreviewWire {  // (a(ssss)a(sssss)a(sss)a(ss)bastxx)
  QList<UpgradeItemWire> upgrades;
  QList<ReplacementWire> replacements;
  QList<NewDependencyWire> new_dependencies;
  QList<HeldBackWire> held_back;
  bool keyring = false;
  QStringList keyring_packages;
  qulonglong download_bytes = 0;
  qlonglong size_change_bytes = 0;
  qlonglong expires_at = 0;  // epoch seconds
};

struct ResultWire {  // (s(sasa(ssas))a(ssss)as)
  QString outcome;
  StatusWire status;
  QList<ChangeWire> changed;
  QStringList warnings;
};

QDBusArgument& operator<<(QDBusArgument& argument, const ChoiceWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, ChoiceWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const StatusWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, StatusWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const PlanItemWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, PlanItemWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const HookItemWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, HookItemWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const PreviewWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, PreviewWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const ChangeWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, ChangeWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const ResultWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, ResultWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const UpgradeItemWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, UpgradeItemWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const ReplacementWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, ReplacementWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const NewDependencyWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, NewDependencyWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const HeldBackWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, HeldBackWire& value);
QDBusArgument& operator<<(QDBusArgument& argument, const UpgradePreviewWire& value);
const QDBusArgument& operator>>(const QDBusArgument& argument, UpgradePreviewWire& value);

// Registers the types with the Qt meta-type and D-Bus systems. Idempotent.
void registerHelperWireTypes();

// Pure decoders: they never throw and never hand a raw helper string to the caller. A reply that cannot be trusted
// (wrong argument count or signature, an unparseable change) gives ServiceError::Kind::NoReply, which the flow shows
// as Unknown for Execute. An unknown status code gives InternalError with no names (REQ-F-047).
// `expected_action` is the wire name of the requested action ("install" or "remove").
[[nodiscard]] holonight_packages_domain::PrepareReply decodePrepareReply(const QDBusMessage& reply,
                                                                         std::string_view expected_action);
// The reply of PrepareUpgrade: status, plan ID and the upgrade preview (system-upgrade REQ-F-019).
[[nodiscard]] holonight_packages_domain::PrepareReply decodePrepareUpgradeReply(const QDBusMessage& reply);
// The ProtocolVersion property read through org.freedesktop.DBus.Properties.Get.
[[nodiscard]] holonight_packages_domain::VersionReply decodeProtocolVersionReply(const QDBusMessage& reply);
[[nodiscard]] holonight_packages_domain::ExecuteReply decodeExecuteReply(const QDBusMessage& reply);
[[nodiscard]] holonight_packages_domain::CancelReply decodeCancelReply(const QDBusMessage& reply);
[[nodiscard]] std::optional<holonight_packages_domain::ProgressUpdate> decodeProgress(const QString& phase,
                                                                                      uint current, uint total,
                                                                                      const QString& item);
// Bus error -> ServiceError (DESIGN section 6.4).
[[nodiscard]] holonight_packages_domain::ServiceError classifyBusError(const QDBusError& error);

}  // namespace helper_wire

Q_DECLARE_METATYPE(helper_wire::ChoiceWire)
Q_DECLARE_METATYPE(helper_wire::StatusWire)
Q_DECLARE_METATYPE(helper_wire::PlanItemWire)
Q_DECLARE_METATYPE(helper_wire::HookItemWire)
Q_DECLARE_METATYPE(helper_wire::PreviewWire)
Q_DECLARE_METATYPE(helper_wire::ChangeWire)
Q_DECLARE_METATYPE(helper_wire::ResultWire)
Q_DECLARE_METATYPE(QList<helper_wire::ChoiceWire>)
Q_DECLARE_METATYPE(QList<helper_wire::PlanItemWire>)
Q_DECLARE_METATYPE(QList<helper_wire::HookItemWire>)
Q_DECLARE_METATYPE(QList<helper_wire::ChangeWire>)
Q_DECLARE_METATYPE(helper_wire::UpgradeItemWire)
Q_DECLARE_METATYPE(helper_wire::ReplacementWire)
Q_DECLARE_METATYPE(helper_wire::NewDependencyWire)
Q_DECLARE_METATYPE(helper_wire::HeldBackWire)
Q_DECLARE_METATYPE(helper_wire::UpgradePreviewWire)
Q_DECLARE_METATYPE(QList<helper_wire::UpgradeItemWire>)
Q_DECLARE_METATYPE(QList<helper_wire::ReplacementWire>)
Q_DECLARE_METATYPE(QList<helper_wire::NewDependencyWire>)
Q_DECLARE_METATYPE(QList<helper_wire::HeldBackWire>)
