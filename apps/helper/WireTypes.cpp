#include "WireTypes.h"

#include <QDBusMetaType>

namespace holonight_helper {
namespace {

using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::PlannedChange;

QString q(std::string_view text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }

QStringList toList(const std::vector<std::string>& values) {
  QStringList list;
  for (const std::string& value : values) {
    list.push_back(QString::fromStdString(value));
  }
  return list;
}

PlanItemWire item(const PlannedChange& change, std::string_view action) {
  return PlanItemWire{
      .name = QString::fromStdString(change.name),
      .action = q(action),
      .old_version = QString::fromStdString(change.old_version),
      .new_version = QString::fromStdString(change.new_version),
      .repository = QString::fromStdString(change.repository),
  };
}

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

void registerWireTypes() {
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

StatusWire okStatus() { return StatusWire{}; }

StatusWire toWire(const holonight_packages_domain::TransactionFailure& failure) {
  StatusWire status;
  status.code = q(holonight_packages_domain::transactionErrorName(failure.code));
  status.names = toList(failure.names);
  for (const auto& choice : failure.choices) {
    status.choices.push_back(ChoiceWire{
        .kind = q(holonight_packages_domain::needsDecisionKindName(choice.kind)),
        .subject = QString::fromStdString(choice.subject),
        .candidates = toList(choice.candidates),
    });
  }
  return status;
}

PreviewWire toWire(const holonight_packages_domain::ResolvedPlan& plan,
                   std::chrono::system_clock::time_point expires_at) {
  PreviewWire preview;
  preview.action = q(holonight_packages_domain::planActionName(plan.action));
  const std::string_view action = plan.isRemoval() ? "remove" : "install";
  for (const PlannedChange& change : plan.changes) {
    switch (change.kind) {
      case ChangeKind::Target:
        preview.targets.push_back(item(change, action));
        break;
      case ChangeKind::Dependency:
        preview.dependencies.push_back(item(change, action));
        break;
      case ChangeKind::Orphan:
        preview.orphans.push_back(item(change, "remove"));
        break;
    }
  }
  for (const auto& hook : plan.hooks) {
    preview.hooks.push_back(HookItemWire{.name = QString::fromStdString(hook.name), .conditional = hook.conditional});
  }
  preview.expires_at = std::chrono::duration_cast<std::chrono::seconds>(expires_at.time_since_epoch()).count();
  return preview;
}

ResultWire toWire(const holonight_packages_domain::ExecutionResult& result) {
  ResultWire wire;
  wire.outcome = q(holonight_packages_domain::executionOutcomeName(result.outcome));
  if (result.outcome == holonight_packages_domain::ExecutionOutcome::Failed) {
    wire.status = toWire(result.failure);
  }
  for (const auto& change : result.changed) {
    wire.changed.push_back(ChangeWire{
        .name = QString::fromStdString(change.name),
        .action = q(holonight_packages_domain::changeActionName(change.action)),
        .old_version = QString::fromStdString(change.old_version),
        .new_version = QString::fromStdString(change.new_version),
    });
  }
  for (const auto token : result.warnings) {
    wire.warnings.push_back(q(holonight_packages_domain::warningTokenName(token)));
  }
  return wire;
}

UpgradePreviewWire toUpgradeWire(const holonight_packages_domain::ResolvedPlan& plan,
                                 std::chrono::system_clock::time_point expires_at) {
  UpgradePreviewWire preview;
  preview.expires_at = std::chrono::duration_cast<std::chrono::seconds>(expires_at.time_since_epoch()).count();
  if (!plan.upgrade) {
    return preview;
  }
  const auto& detail = *plan.upgrade;
  const auto str = [](const std::string& text) { return QString::fromStdString(text); };
  for (const auto& entry : detail.upgrades) {
    preview.upgrades.push_back(UpgradeItemWire{.name = str(entry.name),
                                               .old_version = str(entry.old_version),
                                               .new_version = str(entry.new_version),
                                               .repository = str(entry.repository)});
  }
  for (const auto& entry : detail.replacements) {
    preview.replacements.push_back(ReplacementWire{.old_name = str(entry.old_name),
                                                   .old_version = str(entry.old_version),
                                                   .new_name = str(entry.new_name),
                                                   .new_version = str(entry.new_version),
                                                   .repository = str(entry.repository)});
  }
  for (const auto& entry : detail.new_dependencies) {
    preview.new_dependencies.push_back(
        NewDependencyWire{.name = str(entry.name), .version = str(entry.version), .repository = str(entry.repository)});
  }
  for (const auto& entry : detail.held_back) {
    preview.held_back.push_back(
        HeldBackWire{.name = str(entry.name), .installed_version = str(entry.installed_version)});
  }
  preview.keyring = detail.keyring();
  preview.keyring_packages = toList(detail.keyring_packages);
  preview.download_bytes = detail.download_bytes;
  preview.size_change_bytes = detail.size_change_bytes;
  return preview;
}

}  // namespace holonight_helper
