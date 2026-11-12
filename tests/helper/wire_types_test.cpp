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

// ---- system-upgrade T-048: the upgrade preview ----

TEST(WireTypesTest, TheUpgradePreviewHasTheSignatureOfTheInterfaceXml) {
  registerWireTypes();
  EXPECT_EQ(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<UpgradePreviewWire>())),
            QByteArray("(a(ssss)a(sssss)a(sss)a(ss)bastxx)"));
}

TEST(WireTypesTest, AnUpgradePlanIsMappedFieldByField) {
  registerWireTypes();
  ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  plan.upgrade = holonight_packages_domain::UpgradeDetail{
      .upgrades = {{.name = "app", .old_version = "1", .new_version = "2", .repository = "core"}},
      .replacements =
          {{.old_name = "old", .old_version = "1", .new_name = "new", .new_version = "3", .repository = "extra"}},
      .new_dependencies = {{.name = "dep", .version = "4", .repository = "core"}},
      .held_back = {{.name = "kept", .installed_version = "5"}},
      .keyring_packages = {"archlinux-keyring"},
      .download_bytes = 1234,
      .size_change_bytes = -56,
  };
  const auto expires = std::chrono::system_clock::time_point(std::chrono::seconds(1'700'000'000));
  // The bus round trip is covered by helper_upgrade_dbus_test; here the mapping of every field.
  const UpgradePreviewWire back = toUpgradeWire(plan, expires);
  ASSERT_EQ(back.upgrades.size(), 1);
  EXPECT_EQ(back.upgrades[0].name, QStringLiteral("app"));
  EXPECT_EQ(back.upgrades[0].new_version, QStringLiteral("2"));
  ASSERT_EQ(back.replacements.size(), 1);
  EXPECT_EQ(back.replacements[0].old_name, QStringLiteral("old"));
  EXPECT_EQ(back.replacements[0].new_name, QStringLiteral("new"));
  ASSERT_EQ(back.new_dependencies.size(), 1);
  EXPECT_EQ(back.new_dependencies[0].repository, QStringLiteral("core"));
  ASSERT_EQ(back.held_back.size(), 1);
  EXPECT_EQ(back.held_back[0].installed_version, QStringLiteral("5"));
  EXPECT_TRUE(back.keyring);
  EXPECT_EQ(back.keyring_packages, QStringList{QStringLiteral("archlinux-keyring")});
  EXPECT_EQ(back.download_bytes, 1234U);
  EXPECT_EQ(back.size_change_bytes, -56);
  EXPECT_EQ(back.expires_at, 1'700'000'000);
}

TEST(WireTypesTest, TheCodeTableHasTwentyNineCodesIncludingCancelled) {
  for (std::size_t index = 0; index < holonight_packages_domain::kTransactionErrorCodeCount; ++index) {
    const auto code = static_cast<TransactionErrorCode>(index);
    const StatusWire status = toWire(TransactionFailure{.code = code, .names = {}, .choices = {}});
    EXPECT_FALSE(status.code.isEmpty());
  }
  EXPECT_EQ(holonight_packages_domain::kTransactionErrorCodeCount, 29U);
  EXPECT_EQ(toWire(TransactionFailure{.code = TransactionErrorCode::Cancelled, .names = {}, .choices = {}}).code,
            QStringLiteral("Cancelled"));
}

}  // namespace
}  // namespace holonight_helper
