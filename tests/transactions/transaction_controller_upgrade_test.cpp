#include "TransactionController.h"
#include "TransactionMessages.h"
#include "TransactionRowModel.h"
#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"

#include <QElapsedTimer>

#include <gtest/gtest.h>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;
using State = TransactionController::State;
namespace messages = transaction_messages;

UpgradeDetail detailOf(int upgrades, bool keyring = false) {
  UpgradeDetail detail;
  for (int index = 0; index < upgrades; ++index) {
    detail.upgrades.push_back(UpgradeEntry{
        .name = "pkg-" + std::to_string(index), .old_version = "1", .new_version = "2", .repository = "core"});
  }
  detail.replacements = {ReplacementEntry{
      .old_name = "old", .old_version = "1", .new_name = "new", .new_version = "3", .repository = "core"}};
  detail.new_dependencies = {NewDependencyEntry{.name = "dep", .version = "4", .repository = "core"}};
  detail.held_back = {HeldBackEntry{.name = "kept", .installed_version = "5"}};
  if (keyring) {
    detail.keyring_packages = {"archlinux-keyring"};
  }
  detail.download_bytes = 2048;
  detail.size_change_bytes = 1024;
  return detail;
}

ResolvedPlan planOf(int upgrades, bool keyring = false) {
  ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  plan.upgrade = detailOf(upgrades, keyring);
  return plan;
}

class TransactionControllerUpgrade : public testing::Test {
 public:
  TransactionControllerUpgrade() : controller(service, refresh) {}

  // A click on "Update system…" against a helper of `version`.
  void click(std::uint32_t version = 2) {
    service.setVersion(version);
    ASSERT_TRUE(controller.requestUpgrade());
    service.completeProbes();
  }
  void toPreview(int upgrades = 2, bool keyring = false) {
    click();
    ASSERT_EQ(service.upgradePrepareCalls(), 1U);
    service.completePrepare(FakeTransactionService::previewOf("plan-u", planOf(upgrades, keyring)));
    ASSERT_EQ(controller.state(), State::Previewed);
  }

  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionController controller;
};

// ---- T-066: the protocol probe and the button rules ----

TEST_F(TransactionControllerUpgrade, AnOldHelperDisablesTheButtonWithTheReasonAndIsNeverAskedToPrepare) {
  click(1);
  EXPECT_FALSE(controller.upgradeEnabled());
  EXPECT_EQ(controller.upgradeDisabledReason(), messages::upgradeText(messages::UpgradeKey::HelperTooOld));
  EXPECT_EQ(service.upgradePrepareCalls(), 0U);
  ASSERT_EQ(service.probes().size(), 1U);
  EXPECT_EQ(service.probes().front(), ProbeMode::AutoStart);  // the click may start the helper
}

TEST_F(TransactionControllerUpgrade, ThePageProbesWithoutStartingTheHelper) {
  controller.probeHelper();
  ASSERT_EQ(service.probes().size(), 1U);
  EXPECT_EQ(service.probes().front(), ProbeMode::NoAutoStart);
  service.setVersion(2);
  service.completeProbes();
  EXPECT_TRUE(controller.upgradeEnabled());
  EXPECT_TRUE(service.calls().empty());
}

TEST_F(TransactionControllerUpgrade, VersionTwoEnablesTheButtonWhateverTheListAndARunningTransactionDisablesIt) {
  service.setVersion(2);
  controller.probeHelper();
  service.completeProbes();
  EXPECT_TRUE(controller.upgradeEnabled());  // there is no list input at all
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  EXPECT_FALSE(controller.upgradeEnabled());
}

TEST_F(TransactionControllerUpgrade, AKnownVersionTwoStartsTheUpgradeWithoutAnotherProbe) {
  service.setVersion(2);
  controller.probeHelper();
  service.completeProbes();
  ASSERT_TRUE(controller.requestUpgrade());
  EXPECT_EQ(service.probes().size(), 1U);
  EXPECT_EQ(service.upgradePrepareCalls(), 1U);
  EXPECT_EQ(controller.actionKind(), QStringLiteral("upgrade"));
  EXPECT_EQ(controller.title(), messages::upgradeText(messages::UpgradeKey::Title));
}

TEST_F(TransactionControllerUpgrade, TheSyncingTextCountsTheRepositories) {
  click();
  service.emitProgress("", ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 1, .total = 2, .item = "core"});
  EXPECT_EQ(controller.message(), messages::syncingText(1, 2, "core"));
  EXPECT_TRUE(controller.message().contains(QStringLiteral("1 of 2")));
  service.emitProgress("", ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 2, .total = 2, .item = "extra"});
  EXPECT_TRUE(controller.message().contains(QStringLiteral("2 of 2")));
  EXPECT_TRUE(controller.cancelEnabled());
  controller.cancelRun();
  ASSERT_EQ(service.cancelCalls(), 1U);
  EXPECT_EQ(service.calls().back().plan_id, "");
}

// ---- T-067: the preview rows and properties ----

TEST_F(TransactionControllerUpgrade, ThePreviewHasTheRowsInKindOrderAndTheSizes) {
  toPreview(500);
  const auto& rows = controller.previewRows()->rows();
  ASSERT_EQ(rows.size(), 503);
  EXPECT_EQ(rows.front().kind, QStringLiteral("upgrade"));
  EXPECT_EQ(rows.at(500).kind, QStringLiteral("replacement"));
  EXPECT_EQ(rows.at(500).replaces, QStringLiteral("old"));
  EXPECT_EQ(rows.at(501).kind, QStringLiteral("newDependency"));
  EXPECT_EQ(rows.at(502).kind, QStringLiteral("heldBack"));
  EXPECT_TRUE(controller.keyringNotice().isEmpty());
  EXPECT_FALSE(controller.downloadText().isEmpty());
  EXPECT_FALSE(controller.sizeChangeText().isEmpty());
  EXPECT_EQ(controller.confirmLabel(), messages::upgradeText(messages::UpgradeKey::ConfirmLabel));
  EXPECT_TRUE(controller.isUpgrade());
}

TEST_F(TransactionControllerUpgrade, TheKeyringNoticeNamesTheKeyringPackages) {
  toPreview(1, true);
  EXPECT_TRUE(controller.keyringNotice().contains(QStringLiteral("archlinux-keyring")));
}

TEST_F(TransactionControllerUpgrade, AThousandRowPreviewIsBuiltWithinHalfASecond) {
  const UpgradeDetail detail = detailOf(997);
  QElapsedTimer timer;
  timer.start();
  const auto rows = TransactionRowModel::upgradeRows(detail);
  TransactionRowModel model;
  model.setRows(rows);
  EXPECT_LT(timer.elapsed(), 500);
  EXPECT_EQ(model.rowCount(), 1000);
}

TEST_F(TransactionControllerUpgrade, TheStepTextFollowsTheMarkers) {
  toPreview(1, true);
  controller.confirm();
  service.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::KeyringStep, .current = 1, .total = 2});
  EXPECT_EQ(controller.stepText(), messages::stepText(1, 2));
  service.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::MainStep, .current = 2, .total = 2});
  EXPECT_EQ(controller.stepText(), messages::stepText(2, 2));
}

TEST_F(TransactionControllerUpgrade, ASuccessWithTheKeyringSaysSo) {
  toPreview(1, true);
  controller.confirm();
  service.completeExecute(ExecutionResult::succeeded({ChangedPackage{
      .name = "archlinux-keyring", .action = ChangeAction::Install, .old_version = "1", .new_version = "2"}}));
  EXPECT_TRUE(controller.message().contains(messages::upgradeText(messages::UpgradeKey::Succeeded)));
  EXPECT_TRUE(controller.message().contains(messages::upgradeText(messages::UpgradeKey::KeyringUpdated)));
  EXPECT_EQ(refresh.upgradeCount(), 1);
}

TEST_F(TransactionControllerUpgrade, APartialUpgradeNamesItsStepAndOffersCloseOnly) {
  toPreview();
  controller.confirm();
  auto failed = FakeTransactionService::failed(TransactionErrorCode::UpgradePartiallyApplied);
  failed->failure.names = {"keyring"};
  service.completeExecute(std::move(failed));
  EXPECT_EQ(controller.message(), messages::upgradeText(messages::UpgradeKey::PartialKeyring));
  EXPECT_EQ(controller.actions(), QStringList{QStringLiteral("close")});
}

TEST_F(TransactionControllerUpgrade, NothingToDoSaysTheSystemIsUpToDate) {
  click();
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::NothingToDo));
  EXPECT_EQ(controller.message(), messages::upgradeText(messages::UpgradeKey::SystemUpToDate));
  EXPECT_EQ(controller.actions(), QStringList{QStringLiteral("close")});
  EXPECT_EQ(controller.previewRows()->rowCount(), 0);
}

TEST_F(TransactionControllerUpgrade, TheRefusalButtonStartsTheUpgrade) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PartialUpgradeRequired, {"glibc"}));
  controller.updateSystem();
  EXPECT_EQ(controller.state(), State::Refused);
  EXPECT_FALSE(controller.upgradeEnabled());
  controller.updateSystem();
  ASSERT_EQ(service.probes().size(), 1U);
  service.setVersion(2);
  service.completeProbes();
  EXPECT_EQ(service.upgradePrepareCalls(), 1U);
  EXPECT_EQ(service.prepareCalls(), 2U);
}

TEST_F(TransactionControllerUpgrade, TheRefusalActionProbesAndPreservesTheRefusalForAnOldHelper) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PartialUpgradeRequired, {"glibc"}));
  const auto original = controller.message();
  controller.updateSystem();
  ASSERT_EQ(service.probes().size(), 1U);
  EXPECT_EQ(service.probes().front(), ProbeMode::AutoStart);
  service.setVersion(1);
  service.completeProbes();
  EXPECT_EQ(controller.state(), State::Refused);
  EXPECT_EQ(controller.message(), original);
  EXPECT_FALSE(controller.upgradeEnabled());
  EXPECT_FALSE(controller.upgradeDisabledReason().isEmpty());
  controller.updateSystem();
  EXPECT_EQ(service.upgradePrepareCalls(), 0U);
  EXPECT_EQ(service.probes().size(), 1U);
}

// ---- T-068: the message table ----

TEST(TransactionMessagesUpgrade, EveryUpgradeKeyAndEveryNewCodeHasAnEntry) {
  for (std::size_t index = 0; index < messages::kUpgradeKeyCount; ++index) {
    EXPECT_FALSE(messages::upgradeText(static_cast<messages::UpgradeKey>(index)).isEmpty()) << index;
  }
  for (const auto code : {TransactionErrorCode::SyncFailed, TransactionErrorCode::SyncDatabaseChanged,
                          TransactionErrorCode::KeyImportRequired, TransactionErrorCode::UpgradePartiallyApplied,
                          TransactionErrorCode::Cancelled}) {
    EXPECT_FALSE(messages::failureText(code, {}, PlanAction::Upgrade, true).isEmpty()) << transactionErrorName(code);
  }
  EXPECT_EQ(messages::failureText(TransactionErrorCode::NothingToDo, {}, PlanAction::Upgrade, true),
            messages::upgradeText(messages::UpgradeKey::SystemUpToDate));
  EXPECT_EQ(messages::failureText(TransactionErrorCode::UpgradePartiallyApplied, {"main"}, PlanAction::Upgrade, false),
            messages::upgradeText(messages::UpgradeKey::PartialMain));
}

TEST(TransactionMessagesUpgrade, OnlyNamesAndNumbersAreInserted) {
  // A reply string that is not a step token never reaches the partial-upgrade text.
  const QString partial = messages::failureText(TransactionErrorCode::UpgradePartiallyApplied, {"<b>injected</b>"},
                                                PlanAction::Upgrade, false);
  EXPECT_FALSE(partial.contains(QStringLiteral("injected")));
  EXPECT_EQ(messages::stepText(1, 2), messages::upgradeText(messages::UpgradeKey::Step).arg(1).arg(2));
  EXPECT_TRUE(messages::syncingText(1, 2, "core").endsWith(QStringLiteral("core")));
  // KeyImportRequired names packages, as plain text.
  EXPECT_TRUE(messages::failureText(TransactionErrorCode::KeyImportRequired, {"app"}, PlanAction::Upgrade, true)
                  .contains(QStringLiteral("app")));
}

}  // namespace
