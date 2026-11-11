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

// Registers the types with the Qt meta-type and D-Bus systems. Idempotent.
void registerHelperWireTypes();

// Pure decoders: they never throw and never hand a raw helper string to the caller. A reply that cannot be trusted
// (wrong argument count or signature, an unparseable change) gives ServiceError::Kind::NoReply, which the flow shows
// as Unknown for Execute. An unknown status code gives InternalError with no names (REQ-F-047).
// `expected_action` is the wire name of the requested action ("install" or "remove").
[[nodiscard]] holonight_packages_domain::PrepareReply decodePrepareReply(const QDBusMessage& reply,
                                                                         std::string_view expected_action);
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
