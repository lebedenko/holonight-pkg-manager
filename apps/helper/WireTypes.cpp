#include "WireTypes.h"

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

}  // namespace

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
    preview.upgrades.push_back(UpgradeItemWire{
        .name = str(entry.name),
        .old_version = str(entry.old_version),
        .new_version = str(entry.new_version),
        .repository = str(entry.repository),
    });
  }
  for (const auto& entry : detail.replacements) {
    preview.replacements.push_back(ReplacementWire{
        .old_name = str(entry.old_name),
        .old_version = str(entry.old_version),
        .new_name = str(entry.new_name),
        .new_version = str(entry.new_version),
        .repository = str(entry.repository),
    });
  }
  for (const auto& entry : detail.new_dependencies) {
    preview.new_dependencies.push_back(NewDependencyWire{
        .name = str(entry.name),
        .version = str(entry.version),
        .repository = str(entry.repository),
    });
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
