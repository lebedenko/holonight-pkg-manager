#pragma once

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace helper_wire {

// D-Bus structures declared in apps/helper/dbus/org.holonight.PackageHelper1.xml.
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
