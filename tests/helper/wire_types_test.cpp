#include "WireTypes.h"

#include <QDBusArgument>
#include <QDBusMetaType>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_helper {
namespace {

using holonight_packages_domain::ChangeAction;
using holonight_packages_domain::ChangedPackage;
using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::HookPreview;
using holonight_packages_domain::NeedsDecisionChoice;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlannedChange;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_domain::WarningToken;
using ::testing::ElementsAre;

TEST(WireTypesTest, SignaturesMatchTheInterfaceXml) {
  registerWireTypes();
  EXPECT_EQ(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<StatusWire>())), QByteArray("(sasa(ssas))"));
  EXPECT_EQ(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<PreviewWire>())),
            QByteArray("(sa(sssss)a(sssss)a(sssss)a(sb)x)"));
  EXPECT_EQ(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<ResultWire>())),
            QByteArray("(s(sasa(ssas))a(ssss)as)"));
}

TEST(WireTypesTest, AFailureBecomesACodeNamesAndChoicesOnly) {
  const StatusWire status = toWire(TransactionFailure{
      .code = TransactionErrorCode::NeedsDecision,
      .names = {},
      .choices =
          {
              NeedsDecisionChoice{
                  .kind = NeedsDecisionChoice::Kind::Conflict,
                  .subject = "newpkg",
                  .candidates = {"newpkg", "oldpkg"},
              },
          },
  });
  EXPECT_EQ(status.code, QStringLiteral("NeedsDecision"));
  ASSERT_EQ(status.choices.size(), 1);
  EXPECT_EQ(status.choices[0].kind, QStringLiteral("conflict"));
  EXPECT_EQ(okStatus().code, QString());
}

TEST(WireTypesTest, APreviewSortsChangesIntoTargetsDependenciesAndOrphans) {
  ResolvedPlan plan;
  plan.action = PlanAction::Install;
  plan.changes = {
      PlannedChange{
          .name = "app",
          .kind = ChangeKind::Target,
          .old_version = "",
          .new_version = "1.0-1",
          .repository = "core",
          .download_bytes = 0,
      },
      PlannedChange{
          .name = "lib",
          .kind = ChangeKind::Dependency,
          .old_version = "",
          .new_version = "2.0-1",
          .repository = "core",
          .download_bytes = 0,
      },
      PlannedChange{
          .name = "old",
          .kind = ChangeKind::Orphan,
          .old_version = "0.1-1",
          .new_version = "",
          .repository = "",
          .download_bytes = 0,
      },
  };
  plan.hooks = {HookPreview{.name = "10-a.hook", .conditional = true}};
  const PreviewWire preview = toWire(plan, std::chrono::system_clock::time_point{std::chrono::seconds(1000)});
  EXPECT_EQ(preview.action, QStringLiteral("install"));
  ASSERT_EQ(preview.targets.size(), 1);
  EXPECT_EQ(preview.targets[0].repository, QStringLiteral("core"));
  ASSERT_EQ(preview.dependencies.size(), 1);
  ASSERT_EQ(preview.orphans.size(), 1);
  EXPECT_EQ(preview.orphans[0].action, QStringLiteral("remove"));
  ASSERT_EQ(preview.hooks.size(), 1);
  EXPECT_TRUE(preview.hooks[0].conditional);
  EXPECT_EQ(preview.expires_at, 1000);
}

TEST(WireTypesTest, AResultCarriesOutcomeCodeChangesAndEnumeratedWarnings) {
  ExecutionResult result = ExecutionResult::succeeded(
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1.0-1"}});
  result.warnings = {WarningToken::HookFailed};
  const ResultWire succeeded = toWire(result);
  EXPECT_EQ(succeeded.outcome, QStringLiteral("Succeeded"));
  EXPECT_TRUE(succeeded.status.code.isEmpty());
  ASSERT_EQ(succeeded.changed.size(), 1);
  EXPECT_EQ(succeeded.changed[0].action, QStringLiteral("install"));
  EXPECT_EQ(succeeded.warnings, QStringList{QStringLiteral("HookFailed")});

  const ResultWire failed = toWire(ExecutionResult::failed(TransactionErrorCode::PlanChanged));
  EXPECT_EQ(failed.outcome, QStringLiteral("Failed"));
  EXPECT_EQ(failed.status.code, QStringLiteral("PlanChanged"));
  EXPECT_EQ(toWire(ExecutionResult::cancelled()).outcome, QStringLiteral("Cancelled"));
}

}  // namespace
}  // namespace holonight_helper
