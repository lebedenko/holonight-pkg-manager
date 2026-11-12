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

// The upgrade preview of PrepareUpgrade (system-upgrade DESIGN 6.1).
struct UpgradeItemWire {  // (ssss): name, old version, new version, repository
  QString name;
  QString old_version;
  QString new_version;
  QString repository;
};

struct ReplacementWire {  // (sssss): old name, old version, new name, new version, repository
  QString old_name;
  QString old_version;
  QString new_name;
  QString new_version;
  QString repository;
};

struct NewDependencyWire {  // (sss): name, version, repository
  QString name;
  QString version;
  QString repository;
};

struct HeldBackWire {  // (ss): name, installed version
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
  qlonglong expires_at = 0;
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

// Registers the types with the Qt meta-type and D-Bus systems. Safe to call more than once.
void registerWireTypes();

[[nodiscard]] StatusWire okStatus();
[[nodiscard]] StatusWire toWire(const holonight_packages_domain::TransactionFailure& failure);
[[nodiscard]] PreviewWire toWire(const holonight_packages_domain::ResolvedPlan& plan,
                                 std::chrono::system_clock::time_point expires_at);
[[nodiscard]] ResultWire toWire(const holonight_packages_domain::ExecutionResult& result);
// The preview of an upgrade plan; an empty preview when the plan carries no upgrade detail.
[[nodiscard]] UpgradePreviewWire toUpgradeWire(const holonight_packages_domain::ResolvedPlan& plan,
                                               std::chrono::system_clock::time_point expires_at);

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
Q_DECLARE_METATYPE(holonight_helper::UpgradeItemWire)
Q_DECLARE_METATYPE(holonight_helper::ReplacementWire)
Q_DECLARE_METATYPE(holonight_helper::NewDependencyWire)
Q_DECLARE_METATYPE(holonight_helper::HeldBackWire)
Q_DECLARE_METATYPE(holonight_helper::UpgradePreviewWire)
Q_DECLARE_METATYPE(QList<holonight_helper::UpgradeItemWire>)
Q_DECLARE_METATYPE(QList<holonight_helper::ReplacementWire>)
Q_DECLARE_METATYPE(QList<holonight_helper::NewDependencyWire>)
Q_DECLARE_METATYPE(QList<holonight_helper::HeldBackWire>)
