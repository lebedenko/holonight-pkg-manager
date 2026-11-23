#include "HistoryModel.h"
#include "counting_history_source.h"
#include "history_test_entries.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using holonight_packages_domain::HistoryAction;
using holonight_packages_domain::HistoryActionKind;
using holonight_packages_domain::HistoryEntry;
using holonight_packages_domain::HistoryMessage;
using holonight_packages_domain::HistoryOrigin;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistoryReadMode;
using holonight_packages_domain::HistoryReadResult;
using holonight_packages_domain::HistorySeverity;
using holonight_packages_domain::HistorySource;
using holonight_packages_domain::HistoryStatus;
using holonight_test::CountingHistorySource;
using holonight_test::historyEntry;
using holonight_test::resetWith;
using holonight_test::ScopedTimeZone;
using std::chrono::sys_days;
using ::testing::ElementsAre;
using namespace std::chrono_literals;

constexpr int kWaitMs = 5000;

HistoryModelOptions testOptions() {
  HistoryModelOptions options{.current_uid = 1000, .resolve_user = {}};
  options.resolve_user = [](std::uint32_t uid) -> std::optional<std::string> {
    return uid == 1001 ? std::optional<std::string>("alice") : std::nullopt;
  };
  return options;
}

class HistoryModelTest : public ::testing::Test {
 protected:
  [[nodiscard]] CountingHistorySource& source() { return *source_; }
  [[nodiscard]] HistoryModel& model() { return *model_; }

  [[nodiscard]] bool waitIdle() {
    return QTest::qWaitFor([this] { return model_->loaded() && !model_->loading(); }, kWaitMs);
  }
  [[nodiscard]] QVariant role(int row, int role) const { return model_->data(model_->index(row), role); }

 private:
  std::shared_ptr<CountingHistorySource> source_ = std::make_shared<CountingHistorySource>();
  std::unique_ptr<HistoryModel> model_ = std::make_unique<HistoryModel>(source_, testOptions());
};

TEST_F(HistoryModelTest, NoReadAtConstruction) {
  QTest::qWait(50);
  EXPECT_EQ(source().calls(), 0U);
  EXPECT_FALSE(model().loading());
  EXPECT_FALSE(model().loaded());
  EXPECT_EQ(model().rowCount(), 0);
}

TEST_F(HistoryModelTest, FirstOpenOneFullParse) {
  source().enqueue(resetWith({historyEntry(sys_days{2026y / 11 / 23}, 10h)}));
  model().ensureLoaded();
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());
  model().ensureLoaded();
  QTest::qWait(20);
  EXPECT_THAT(source().modes(), ElementsAre(HistoryReadMode::Full));
  EXPECT_EQ(model().rowCount(), 1);
  EXPECT_EQ(model().entryCount(), 1);
}

TEST_F(HistoryModelTest, ReadsRunOffTheMainThread) {
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());
  model().refresh();
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 2 && !model().loading(); }, kWaitMs));
  const auto main_thread = std::this_thread::get_id();
  ASSERT_EQ(source().threads().size(), 2U);
  for (const auto& thread : source().threads()) {
    EXPECT_NE(thread, main_thread);
  }
  EXPECT_THAT(source().modes(), ElementsAre(HistoryReadMode::Full, HistoryReadMode::Incremental));
}

TEST_F(HistoryModelTest, GroupsByDayNewestFirst) {
  const ScopedTimeZone zone("UTC0");
  source().enqueue(resetWith({
      historyEntry(sys_days{2026y / 11 / 21}, 9h),
      historyEntry(sys_days{2026y / 11 / 21}, 18h),
      historyEntry(sys_days{2026y / 11 / 22}, 7h),
      historyEntry(sys_days{2026y / 11 / 23}, 8h),
      historyEntry(sys_days{2026y / 11 / 23}, 20h),
  }));
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());
  ASSERT_EQ(model().rowCount(), 5);
  QStringList days;
  for (int row = 0; row < model().rowCount(); ++row) {
    days.append(role(row, HistoryModel::DayKeyRole).toString());
    if (row > 0) {
      EXPECT_GT(role(row - 1, HistoryModel::StartedAtRole).toDateTime(),
                role(row, HistoryModel::StartedAtRole).toDateTime());
    }
  }
  EXPECT_EQ(days, (QStringList{"2026-11-23", "2026-11-23", "2026-11-22", "2026-11-21", "2026-11-21"}));
  EXPECT_EQ(role(0, HistoryModel::TimeLabelRole).toString(), QStringLiteral("20:00"));
}

TEST_F(HistoryModelTest, RowRoles) {
  const ScopedTimeZone zone("UTC0");
  HistoryEntry pacman = historyEntry(sys_days{2026y / 11 / 23}, 10h + 5min, {"linux", "linux-headers"});
  pacman.origin = {
      .kind = HistoryOriginKind::Pacman,
      .uid = std::nullopt,
      .command = "pacman --sync --refresh --sysupgrade",
  };
  pacman.actions.push_back({
      .kind = HistoryActionKind::Removed,
      .name = "old",
      .old_version = "3-1",
      .new_version = {},
  });
  pacman.pre_hooks = {"71-dkms-remove.hook"};
  pacman.hooks = {"30-systemd-update.hook"};
  pacman.messages = {HistoryMessage{.severity = HistorySeverity::Warning, .text = "w"}};
  HistoryEntry by_alice = historyEntry(sys_days{2026y / 11 / 23}, 11h);
  by_alice.origin = {.kind = HistoryOriginKind::Packages, .uid = 1001, .command = {}};
  by_alice.status = HistoryStatus::InProgress;
  source().enqueue(resetWith({pacman, by_alice}));
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());

  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return role(0, HistoryModel::OriginLabelRole).toString() == QStringLiteral("Packages · alice"); },
      kWaitMs));
  EXPECT_EQ(role(0, HistoryModel::OriginKindRole).toString(), QStringLiteral("packages"));
  EXPECT_EQ(role(0, HistoryModel::CommandLineRole).toString(), QString());
  EXPECT_EQ(role(0, HistoryModel::StatusKindRole).toString(), QStringLiteral("inProgress"));
  EXPECT_EQ(role(0, HistoryModel::StatusLabelRole).toString(), QStringLiteral("In progress"));

  EXPECT_EQ(role(1, HistoryModel::TimeLabelRole).toString(), QStringLiteral("10:05"));
  EXPECT_EQ(role(1, HistoryModel::OriginLabelRole).toString(), QStringLiteral("pacman"));
  EXPECT_EQ(role(1, HistoryModel::CommandLineRole).toString(), QStringLiteral("pacman --sync --refresh --sysupgrade"));
  EXPECT_EQ(role(1, HistoryModel::SummaryRole).toString(), QStringLiteral("2 upgraded · 1 removed"));
  EXPECT_EQ(role(1, HistoryModel::StatusLabelRole).toString(), QStringLiteral("Completed"));
  EXPECT_EQ(role(1, HistoryModel::PreHooksRole).toStringList(), QStringList{"71-dkms-remove.hook"});
  EXPECT_EQ(role(1, HistoryModel::HooksRole).toStringList(), QStringList{"30-systemd-update.hook"});
  EXPECT_EQ(role(1, HistoryModel::PackageNamesRole).toStringList(), (QStringList{"linux", "linux-headers", "old"}));
  const QVariantList actions = role(1, HistoryModel::ActionsRole).toList();
  ASSERT_EQ(actions.size(), 3);
  EXPECT_EQ(actions[0].toMap().value("versionText").toString(), QStringLiteral("1-1 → 2-1"));
  EXPECT_EQ(actions[2].toMap().value("versionText").toString(), QStringLiteral("3-1"));
  EXPECT_EQ(actions[2].toMap().value("kindLabel").toString(), QStringLiteral("removed"));
  const QVariantList messages = role(1, HistoryModel::MessagesRole).toList();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0].toMap().value("severity").toString(), QStringLiteral("warning"));
  EXPECT_EQ(role(1, HistoryModel::SequenceRole).value<quint32>(), 0U);
  EXPECT_EQ(role(0, HistoryModel::SequenceRole).value<quint32>(), 1U);
}

TEST_F(HistoryModelTest, DeltaReplacesTheLastEntryInPlaceAndInsertsNewRowsAtTheTop) {
  source().enqueue(
      resetWith({historyEntry(sys_days{2026y / 11 / 22}, 1h), historyEntry(sys_days{2026y / 11 / 23}, 1h)}));
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());

  HistoryEntry revised = historyEntry(sys_days{2026y / 11 / 23}, 1h);
  revised.hooks = {"post.hook"};
  HistoryReadResult delta;
  delta.drop_last = 1;
  delta.append = {revised, historyEntry(sys_days{2026y / 11 / 24}, 1h, {"bar"})};
  source().enqueue(delta);
  QSignalSpy changed(&model(), &QAbstractItemModel::dataChanged);
  QSignalSpy removed(&model(), &QAbstractItemModel::rowsRemoved);
  QSignalSpy inserted(&model(), &QAbstractItemModel::rowsInserted);
  QSignalSpy reset(&model(), &QAbstractItemModel::modelReset);
  model().refresh();
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 2 && !model().loading(); }, kWaitMs));

  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(removed.count(), 0);
  EXPECT_EQ(inserted.count(), 1);
  EXPECT_EQ(reset.count(), 0);
  ASSERT_EQ(model().rowCount(), 3);
  EXPECT_EQ(role(0, HistoryModel::PackageNamesRole).toStringList(), QStringList{"bar"});
  EXPECT_EQ(role(1, HistoryModel::HooksRole).toStringList(), QStringList{"post.hook"});
  EXPECT_EQ(role(1, HistoryModel::SequenceRole).value<quint32>(), 1U);
}

TEST_F(HistoryModelTest, DropLastWithoutReplacementRemovesTheRow) {
  source().enqueue(
      resetWith({historyEntry(sys_days{2026y / 11 / 22}, 1h), historyEntry(sys_days{2026y / 11 / 23}, 1h)}));
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());
  HistoryReadResult delta;
  delta.drop_last = 1;
  source().enqueue(delta);
  model().refresh();
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 2 && !model().loading(); }, kWaitMs));
  EXPECT_EQ(model().rowCount(), 1);
}

TEST_F(HistoryModelTest, RefreshBeforeLoadIsNoop) {
  model().refreshIfLoaded();
  QTest::qWait(50);
  EXPECT_EQ(source().calls(), 0U);
  EXPECT_FALSE(model().loaded());
}

TEST_F(HistoryModelTest, RefreshIsIncremental) {
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());
  model().refreshIfLoaded();
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 2 && !model().loading(); }, kWaitMs));
  EXPECT_THAT(source().modes(), ElementsAre(HistoryReadMode::Full, HistoryReadMode::Incremental));
}

TEST_F(HistoryModelTest, RefreshBeforeTheFirstLoadLoads) {
  model().refresh();
  ASSERT_TRUE(waitIdle());
  EXPECT_THAT(source().modes(), ElementsAre(HistoryReadMode::Full));
}

TEST_F(HistoryModelTest, RefreshDuringReadRepeatsOnce) {
  source().block();
  model().ensureLoaded();
  source().waitForStarted(1);
  EXPECT_TRUE(model().loading());
  model().refresh();
  model().refresh();
  model().refreshIfLoaded();
  source().release();
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 2 && !model().loading(); }, kWaitMs));
  QTest::qWait(50);
  EXPECT_THAT(source().modes(), ElementsAre(HistoryReadMode::Full, HistoryReadMode::Incremental));
}

TEST_F(HistoryModelTest, EmptyReasonIsPublished) {
  HistoryReadResult missing;
  missing.reset = true;
  missing.reason = "The pacman log /x does not exist.";
  source().enqueue(missing);
  model().ensureLoaded();
  ASSERT_TRUE(waitIdle());
  EXPECT_EQ(model().emptyReason(), QStringLiteral("The pacman log /x does not exist."));
  EXPECT_EQ(model().entryCount(), 0);
}

class ThrowingHistorySource final : public HistorySource {
 public:
  HistoryReadResult read(HistoryReadMode /*mode*/) override { throw std::runtime_error("boom"); }
};

TEST(HistoryModelFailure, AnExceptionBecomesAnEmptyStateReason) {
  HistoryModel model(std::make_shared<ThrowingHistorySource>(), testOptions());
  model.ensureLoaded();
  ASSERT_TRUE(QTest::qWaitFor([&model] { return model.loaded(); }, kWaitMs));
  EXPECT_EQ(model.emptyReason(), QStringLiteral("Reading the pacman log failed: boom"));
}

TEST(HistoryModelFormat, FormatDayUsesTheLocaleLongDate) {
  EXPECT_FALSE(HistoryModel::formatDay(QStringLiteral("2026-11-23")).isEmpty());
  EXPECT_NE(HistoryModel::formatDay(QStringLiteral("2026-11-23")), QStringLiteral("2026-11-23"));
  EXPECT_EQ(HistoryModel::formatDay(QStringLiteral("not a day")), QStringLiteral("not a day"));
}

TEST(HistoryUserResolution, ForeignUsersUseTheFallbackWhileResolvingAndCacheAcrossReloads) {
  struct Resolver {
    std::atomic<int> calls{0};
    std::binary_semaphore resume{0};
  };
  const auto resolver = std::make_shared<Resolver>();
  auto source = std::make_shared<CountingHistorySource>();
  auto foreign = historyEntry(sys_days{2026y / 11 / 23}, 1h);
  foreign.origin = {.kind = HistoryOriginKind::Packages, .uid = 1001, .command = {}};
  auto unresolved = foreign;
  unresolved.origin.uid = 1002;
  const auto own = historyEntry(sys_days{2026y / 11 / 23}, 2h);
  const auto fixture = resetWith({foreign, own, unresolved, foreign});
  source->enqueue(fixture);
  const auto resolve = [resolver](std::uint32_t uid) -> std::optional<std::string> {
    ++resolver->calls;
    if (uid == 1001) {
      static_cast<void>(resolver->resume.try_acquire_for(5s));
      return "alice";
    }
    return std::nullopt;
  };
  HistoryModel model(source, {.current_uid = 1000, .resolve_user = resolve});
  QSignalSpy changed(&model, &HistoryModel::dataChanged);
  model.ensureLoaded();
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.loaded() && resolver->calls == 2; }, kWaitMs));
  EXPECT_EQ(model.index(0).data(HistoryModel::OriginLabelRole).toString(), QStringLiteral("Packages · uid 1001"));
  EXPECT_EQ(model.index(1).data(HistoryModel::OriginLabelRole).toString(), QStringLiteral("Packages · uid 1002"));
  EXPECT_EQ(changed.count(), 0);
  resolver->resume.release();

  ASSERT_TRUE(QTest::qWaitFor([&] { return changed.count() == 2; }, kWaitMs));
  EXPECT_EQ(changed.at(0).at(0).value<QModelIndex>().row(), 0);
  EXPECT_EQ(changed.at(1).at(0).value<QModelIndex>().row(), 3);
  for (const auto& signal : changed) {
    EXPECT_EQ(signal.at(2).value<QList<int>>(), QList<int>{HistoryModel::OriginLabelRole});
  }
  EXPECT_EQ(model.index(0).data(HistoryModel::OriginLabelRole).toString(), QStringLiteral("Packages · alice"));
  EXPECT_EQ(model.index(1).data(HistoryModel::OriginLabelRole).toString(), QStringLiteral("Packages · uid 1002"));
  EXPECT_EQ(resolver->calls, 2);

  source->enqueue(fixture);
  model.refresh();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model.loading(); }, kWaitMs));
  EXPECT_EQ(model.index(0).data(HistoryModel::OriginLabelRole).toString(), QStringLiteral("Packages · alice"));
  EXPECT_EQ(resolver->calls, 2);
}

}  // namespace
