#include "HelperWire.h"

#include <QDBusMetaType>
#include <QVariant>

#include <chrono>
#include <string>
#include <utility>

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

namespace {

using holonight_packages_domain::ChangedPackage;
using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::ExecuteReply;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::NeedsDecisionChoice;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlannedChange;
using holonight_packages_domain::PreparedPlan;
using holonight_packages_domain::ServiceError;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

constexpr const char* kStatusSignature = "(sasa(ssas))";
constexpr const char* kPreviewSignature = "(sa(sssss)a(sssss)a(sssss)a(sb)x)";
constexpr const char* kResultSignature = "(s(sasa(ssas))a(ssss)as)";
constexpr const char* kUpgradePreviewSignature = "(a(ssss)a(sssss)a(sss)a(ss)bastxx)";

ServiceError noReply() { return ServiceError{.kind = ServiceError::Kind::NoReply, .failure = {}}; }

std::string s(const QString& value) { return value.toStdString(); }

// Extracts T from either a ready value (in-process messages) or a demarshalled QDBusArgument, checking the signature
// so that a reply of another shape is rejected instead of misread.
template <typename T>
bool extract(const QVariant& variant, const char* signature, T& out) {
  if (variant.metaType() == QMetaType::fromType<T>()) {
    out = variant.value<T>();
    return true;
  }
  if (variant.metaType() != QMetaType::fromType<QDBusArgument>()) {
    return false;
  }
  const auto argument = variant.value<QDBusArgument>();
  if (argument.currentSignature() != QLatin1String(signature)) {
    return false;
  }
  argument >> out;
  return true;
}

bool extractString(const QVariant& variant, QString& out) {
  if (variant.metaType() != QMetaType::fromType<QString>()) {
    return false;
  }
  out = variant.toString();
  return true;
}

TransactionFailure toFailure(const StatusWire& status) {
  const auto code = holonight_packages_domain::parseTransactionErrorCode(s(status.code));
  if (!code) {
    // An unknown code never reaches the user: it is an internal error without names (REQ-F-047).
    return TransactionFailure{.code = TransactionErrorCode::InternalError, .names = {}, .choices = {}};
  }
  TransactionFailure failure{.code = *code, .names = {}, .choices = {}};
  for (const QString& name : status.names) {
    failure.names.push_back(s(name));
  }
  for (const ChoiceWire& choice : status.choices) {
    NeedsDecisionChoice::Kind kind = NeedsDecisionChoice::Kind::Provider;
    if (choice.kind == QLatin1String("conflict")) {
      kind = NeedsDecisionChoice::Kind::Conflict;
    } else if (choice.kind == QLatin1String("replace")) {
      kind = NeedsDecisionChoice::Kind::Replace;
    }
    NeedsDecisionChoice decoded{.kind = kind, .subject = s(choice.subject), .candidates = {}};
    for (const QString& candidate : choice.candidates) {
      decoded.candidates.push_back(s(candidate));
    }
    failure.choices.push_back(std::move(decoded));
  }
  return failure;
}

ServiceError refusal(const StatusWire& status) {
  return ServiceError{.kind = ServiceError::Kind::Refused, .failure = toFailure(status)};
}

PlannedChange toChange(const PlanItemWire& item, ChangeKind kind) {
  return PlannedChange{
      .name = s(item.name),
      .kind = kind,
      .old_version = s(item.old_version),
      .new_version = s(item.new_version),
      .repository = s(item.repository),
      .download_bytes = 0,
  };
}

bool replyIsUsable(const QDBusMessage& reply, qsizetype argument_count) {
  return reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() == argument_count;
}

}  // namespace

holonight_packages_domain::PrepareReply decodePrepareReply(const QDBusMessage& reply,
                                                           std::string_view expected_action) {
  if (reply.type() == QDBusMessage::ErrorMessage) {
    return std::unexpected(classifyBusError(QDBusError(reply)));
  }
  if (!replyIsUsable(reply, 3)) {
    return std::unexpected(noReply());
  }
  const QVariantList arguments = reply.arguments();
  StatusWire status;
  QString plan_id;
  PreviewWire preview;
  if (!extract(arguments.at(0), kStatusSignature, status) || !extractString(arguments.at(1), plan_id) ||
      !extract(arguments.at(2), kPreviewSignature, preview)) {
    return std::unexpected(noReply());
  }
  if (!status.code.isEmpty()) {
    return std::unexpected(refusal(status));
  }
  if (plan_id.isEmpty() || s(preview.action) != expected_action) {
    return std::unexpected(noReply());
  }
  holonight_packages_domain::ResolvedPlan plan;
  plan.action = preview.action == QLatin1String("remove") ? PlanAction::Remove : PlanAction::Install;
  for (const PlanItemWire& item : preview.targets) {
    plan.changes.push_back(toChange(item, ChangeKind::Target));
  }
  for (const PlanItemWire& item : preview.dependencies) {
    plan.changes.push_back(toChange(item, ChangeKind::Dependency));
  }
  for (const PlanItemWire& item : preview.orphans) {
    plan.changes.push_back(toChange(item, ChangeKind::Orphan));
  }
  for (const HookItemWire& hook : preview.hooks) {
    plan.hooks.push_back(holonight_packages_domain::HookPreview{.name = s(hook.name), .conditional = hook.conditional});
  }
  return PreparedPlan{
      .plan_id = s(plan_id),
      .plan = std::move(plan),
      .expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{preview.expires_at}},
  };
}

holonight_packages_domain::PrepareReply decodePrepareUpgradeReply(const QDBusMessage& reply) {
  if (reply.type() == QDBusMessage::ErrorMessage) {
    return std::unexpected(classifyBusError(QDBusError(reply)));
  }
  if (!replyIsUsable(reply, 3)) {
    return std::unexpected(noReply());
  }
  const QVariantList arguments = reply.arguments();
  StatusWire status;
  QString plan_id;
  UpgradePreviewWire preview;
  if (!extract(arguments.at(0), kStatusSignature, status) || !extractString(arguments.at(1), plan_id) ||
      !extract(arguments.at(2), kUpgradePreviewSignature, preview)) {
    return std::unexpected(noReply());
  }
  if (!status.code.isEmpty()) {
    return std::unexpected(refusal(status));
  }
  if (plan_id.isEmpty()) {
    return std::unexpected(noReply());
  }
  holonight_packages_domain::UpgradeDetail detail;
  for (const UpgradeItemWire& item : preview.upgrades) {
    detail.upgrades.push_back({.name = s(item.name),
                               .old_version = s(item.old_version),
                               .new_version = s(item.new_version),
                               .repository = s(item.repository)});
  }
  for (const ReplacementWire& item : preview.replacements) {
    detail.replacements.push_back({.old_name = s(item.old_name),
                                   .old_version = s(item.old_version),
                                   .new_name = s(item.new_name),
                                   .new_version = s(item.new_version),
                                   .repository = s(item.repository)});
  }
  for (const NewDependencyWire& item : preview.new_dependencies) {
    detail.new_dependencies.push_back(
        {.name = s(item.name), .version = s(item.version), .repository = s(item.repository)});
  }
  for (const HeldBackWire& item : preview.held_back) {
    detail.held_back.push_back({.name = s(item.name), .installed_version = s(item.installed_version)});
  }
  // The keyring list is the flag: a set flag without packages, or packages without the flag, is not trusted.
  if (preview.keyring == preview.keyring_packages.isEmpty()) {
    return std::unexpected(noReply());
  }
  for (const QString& name : preview.keyring_packages) {
    detail.keyring_packages.push_back(s(name));
  }
  detail.download_bytes = preview.download_bytes;
  detail.size_change_bytes = preview.size_change_bytes;
  holonight_packages_domain::ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  plan.upgrade = std::move(detail);
  return PreparedPlan{
      .plan_id = s(plan_id),
      .plan = std::move(plan),
      .expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{preview.expires_at}},
  };
}

holonight_packages_domain::VersionReply decodeProtocolVersionReply(const QDBusMessage& reply) {
  if (reply.type() == QDBusMessage::ErrorMessage) {
    return std::unexpected(classifyBusError(QDBusError(reply)));
  }
  if (!replyIsUsable(reply, 1)) {
    return std::unexpected(noReply());
  }
  const QVariant value = reply.arguments().at(0).value<QDBusVariant>().variant();
  if (value.metaType() != QMetaType::fromType<uint>()) {
    return std::unexpected(noReply());
  }
  return static_cast<std::uint32_t>(value.toUInt());
}

ExecuteReply decodeExecuteReply(const QDBusMessage& reply) {
  if (reply.type() == QDBusMessage::ErrorMessage) {
    return std::unexpected(classifyBusError(QDBusError(reply)));
  }
  if (!replyIsUsable(reply, 1)) {
    return std::unexpected(noReply());
  }
  ResultWire wire;
  if (!extract(reply.arguments().at(0), kResultSignature, wire)) {
    return std::unexpected(noReply());
  }
  ExecutionResult result;
  const auto outcome = holonight_packages_domain::parseExecutionOutcome(s(wire.outcome));
  if (!outcome) {
    return ExecutionResult::failed(TransactionErrorCode::InternalError);
  }
  result.outcome = *outcome;
  if (*outcome == holonight_packages_domain::ExecutionOutcome::Failed) {
    result.failure = wire.status.code.isEmpty()
                         ? TransactionFailure{.code = TransactionErrorCode::InternalError, .names = {}, .choices = {}}
                         : toFailure(wire.status);
  }
  for (const ChangeWire& change : wire.changed) {
    const auto action = holonight_packages_domain::parseChangeAction(s(change.action));
    if (!action) {
      return std::unexpected(noReply());  // never drop a change silently
    }
    result.changed.push_back(ChangedPackage{
        .name = s(change.name),
        .action = *action,
        .old_version = s(change.old_version),
        .new_version = s(change.new_version),
    });
  }
  for (const QString& warning : wire.warnings) {
    if (const auto token = holonight_packages_domain::parseWarningToken(s(warning))) {
      result.warnings.push_back(*token);
    }
  }
  return result;
}

holonight_packages_domain::CancelReply decodeCancelReply(const QDBusMessage& reply) {
  if (reply.type() == QDBusMessage::ErrorMessage) {
    return std::unexpected(classifyBusError(QDBusError(reply)));
  }
  if (!replyIsUsable(reply, 1)) {
    return std::unexpected(noReply());
  }
  StatusWire status;
  if (!extract(reply.arguments().at(0), kStatusSignature, status)) {
    return std::unexpected(noReply());
  }
  if (!status.code.isEmpty()) {
    return std::unexpected(refusal(status));
  }
  return {};
}

std::optional<holonight_packages_domain::ProgressUpdate> decodeProgress(const QString& phase, uint current, uint total,
                                                                        const QString& item) {
  const auto parsed = holonight_packages_domain::parseProgressPhase(s(phase));
  if (!parsed) {
    return std::nullopt;
  }
  return holonight_packages_domain::ProgressUpdate{
      .phase = *parsed,
      .current = current,
      .total = total,
      .item = s(item),
  };
}

holonight_packages_domain::ServiceError classifyBusError(const QDBusError& error) {
  const QString name = error.name();
  if (error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::NoServer ||
      name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner") ||
      name.startsWith(QLatin1String("org.freedesktop.DBus.Error.Spawn."))) {
    return ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}};
  }
  switch (error.type()) {
    case QDBusError::NoReply:
    case QDBusError::Timeout:
    case QDBusError::TimedOut:
    case QDBusError::Disconnected:
      return noReply();
    default:
      break;
  }
  return ServiceError{.kind = ServiceError::Kind::Rejected, .failure = {}};
}

}  // namespace helper_wire
