#include "HelperProtocol.h"

#include <QDBusMetaType>

namespace helper_wire {

namespace {

template <typename T>
void writeList(QDBusArgument& argument, const QList<T>& values) {
  argument.beginArray(QMetaType::fromType<T>());
  for (const T& value : values) {
    argument << value;
  }
  argument.endArray();
}

template <typename T>
void readList(const QDBusArgument& argument, QList<T>& values) {
  argument.beginArray();
  values.clear();
  while (!argument.atEnd()) {
    T value;
    argument >> value;
    values.push_back(value);
  }
  argument.endArray();
}

}  // namespace

QDBusArgument& operator<<(QDBusArgument& argument, const ChoiceWire& value) {
  argument.beginStructure();
  argument << value.kind << value.subject << value.candidates;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, ChoiceWire& value) {
  argument.beginStructure();
  argument >> value.kind >> value.subject >> value.candidates;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const StatusWire& value) {
  argument.beginStructure();
  argument << value.code << value.names;
  writeList(argument, value.choices);
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, StatusWire& value) {
  argument.beginStructure();
  argument >> value.code >> value.names;
  readList(argument, value.choices);
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const PlanItemWire& value) {
  argument.beginStructure();
  argument << value.name << value.action << value.old_version << value.new_version << value.repository;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, PlanItemWire& value) {
  argument.beginStructure();
  argument >> value.name >> value.action >> value.old_version >> value.new_version >> value.repository;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const HookItemWire& value) {
  argument.beginStructure();
  argument << value.name << value.conditional;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, HookItemWire& value) {
  argument.beginStructure();
  argument >> value.name >> value.conditional;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const PreviewWire& value) {
  argument.beginStructure();
  argument << value.action;
  writeList(argument, value.targets);
  writeList(argument, value.dependencies);
  writeList(argument, value.orphans);
  writeList(argument, value.hooks);
  argument << value.expires_at;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, PreviewWire& value) {
  argument.beginStructure();
  argument >> value.action;
  readList(argument, value.targets);
  readList(argument, value.dependencies);
  readList(argument, value.orphans);
  readList(argument, value.hooks);
  argument >> value.expires_at;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const ChangeWire& value) {
  argument.beginStructure();
  argument << value.name << value.action << value.old_version << value.new_version;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, ChangeWire& value) {
  argument.beginStructure();
  argument >> value.name >> value.action >> value.old_version >> value.new_version;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const ResultWire& value) {
  argument.beginStructure();
  argument << value.outcome << value.status;
  writeList(argument, value.changed);
  argument << value.warnings;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, ResultWire& value) {
  argument.beginStructure();
  argument >> value.outcome >> value.status;
  readList(argument, value.changed);
  argument >> value.warnings;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const UpgradeItemWire& value) {
  argument.beginStructure();
  argument << value.name << value.old_version << value.new_version << value.repository;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, UpgradeItemWire& value) {
  argument.beginStructure();
  argument >> value.name >> value.old_version >> value.new_version >> value.repository;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const ReplacementWire& value) {
  argument.beginStructure();
  argument << value.old_name << value.old_version << value.new_name << value.new_version << value.repository;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, ReplacementWire& value) {
  argument.beginStructure();
  argument >> value.old_name >> value.old_version >> value.new_name >> value.new_version >> value.repository;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const NewDependencyWire& value) {
  argument.beginStructure();
  argument << value.name << value.version << value.repository;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, NewDependencyWire& value) {
  argument.beginStructure();
  argument >> value.name >> value.version >> value.repository;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const HeldBackWire& value) {
  argument.beginStructure();
  argument << value.name << value.installed_version;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, HeldBackWire& value) {
  argument.beginStructure();
  argument >> value.name >> value.installed_version;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const UpgradePreviewWire& value) {
  argument.beginStructure();
  writeList(argument, value.upgrades);
  writeList(argument, value.replacements);
  writeList(argument, value.new_dependencies);
  writeList(argument, value.held_back);
  argument << value.keyring << value.keyring_packages << value.download_bytes << value.size_change_bytes
           << value.expires_at;
  argument.endStructure();
  return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, UpgradePreviewWire& value) {
  argument.beginStructure();
  readList(argument, value.upgrades);
  readList(argument, value.replacements);
  readList(argument, value.new_dependencies);
  readList(argument, value.held_back);
  argument >> value.keyring >> value.keyring_packages >> value.download_bytes >> value.size_change_bytes >>
      value.expires_at;
  argument.endStructure();
  return argument;
}

void registerHelperWireTypes() {
  static const bool registered = [] {
    qDBusRegisterMetaType<ChoiceWire>();
    qDBusRegisterMetaType<QList<ChoiceWire>>();
    qDBusRegisterMetaType<StatusWire>();
    qDBusRegisterMetaType<PlanItemWire>();
    qDBusRegisterMetaType<QList<PlanItemWire>>();
    qDBusRegisterMetaType<HookItemWire>();
    qDBusRegisterMetaType<QList<HookItemWire>>();
    qDBusRegisterMetaType<PreviewWire>();
    qDBusRegisterMetaType<ChangeWire>();
    qDBusRegisterMetaType<QList<ChangeWire>>();
    qDBusRegisterMetaType<ResultWire>();
    qDBusRegisterMetaType<UpgradeItemWire>();
    qDBusRegisterMetaType<QList<UpgradeItemWire>>();
    qDBusRegisterMetaType<ReplacementWire>();
    qDBusRegisterMetaType<QList<ReplacementWire>>();
    qDBusRegisterMetaType<NewDependencyWire>();
    qDBusRegisterMetaType<QList<NewDependencyWire>>();
    qDBusRegisterMetaType<HeldBackWire>();
    qDBusRegisterMetaType<QList<HeldBackWire>>();
    qDBusRegisterMetaType<UpgradePreviewWire>();
    return true;
  }();
  static_cast<void>(registered);
}

}  // namespace helper_wire
