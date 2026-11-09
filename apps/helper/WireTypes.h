#pragma once

#include "holonight_packages_domain/transaction_engine.h"

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <chrono>

namespace holonight_helper {

// The structures of dbus/org.holonight.PackageHelper1.xml. A reply carries only enumerated codes, package names,
// versions and integers; no free text ever travels (REQ-F-051).

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
  qlonglong expires_at = 0;
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

// Registers the types with the Qt meta-type and D-Bus systems. Safe to call more than once.
void registerWireTypes();

[[nodiscard]] StatusWire okStatus();
[[nodiscard]] StatusWire toWire(const holonight_packages_domain::TransactionFailure& failure);
[[nodiscard]] PreviewWire toWire(const holonight_packages_domain::ResolvedPlan& plan,
                                 std::chrono::system_clock::time_point expires_at);
[[nodiscard]] ResultWire toWire(const holonight_packages_domain::ExecutionResult& result);

}  // namespace holonight_helper

Q_DECLARE_METATYPE(holonight_helper::ChoiceWire)
Q_DECLARE_METATYPE(holonight_helper::StatusWire)
Q_DECLARE_METATYPE(holonight_helper::PlanItemWire)
Q_DECLARE_METATYPE(holonight_helper::HookItemWire)
Q_DECLARE_METATYPE(holonight_helper::PreviewWire)
Q_DECLARE_METATYPE(holonight_helper::ChangeWire)
Q_DECLARE_METATYPE(holonight_helper::ResultWire)
Q_DECLARE_METATYPE(QList<holonight_helper::ChoiceWire>)
Q_DECLARE_METATYPE(QList<holonight_helper::PlanItemWire>)
Q_DECLARE_METATYPE(QList<holonight_helper::HookItemWire>)
Q_DECLARE_METATYPE(QList<holonight_helper::ChangeWire>)
