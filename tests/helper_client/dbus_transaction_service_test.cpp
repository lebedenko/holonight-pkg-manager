#include "DBusTransactionService.h"
#include "helper_bus_fixture.h"

#include <QElapsedTimer>
#include <QTimer>

#include <optional>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_testing::FakeHelperObject;
using holonight_packages_testing::HelperBusFixture;
using Mode = FakeHelperObject::Mode;

helper_wire::PreviewWire installPreview() {
  helper_wire::PreviewWire preview;
  preview.action = QStringLiteral("install");
  preview.targets.push_back({
      .name = QStringLiteral("app"),
      .action = QStringLiteral("install"),
      .old_version = QString(),
      .new_version = QStringLiteral("1-1"),
      .repository = QStringLiteral("extra"),
  });
  preview.expires_at = 1700000300;
  return preview;
}

QVariantList preparedReply(const QString& plan_id = QStringLiteral("plan-1")) {
  return {QVariant::fromValue(helper_wire::StatusWire{}), plan_id, QVariant::fromValue(installPreview())};
}

class DBusTransactionServiceCalls : public HelperBusFixture {
 protected:
  std::unique_ptr<DBusTransactionService> makeService(DBusTransactionService::Timeouts timeouts = {}) {
    return std::make_unique<DBusTransactionService>([this] { return connect(QStringLiteral("adapter")); }, timeouts);
  }

  template <typename Reply>
  struct Captured {
    std::optional<Reply> reply;
    [[nodiscard]] bool done() const { return reply.has_value(); }
  };

  static bool waitFor(const std::function<bool()>& condition, int timeout_ms = 5000) {
    return QTest::qWaitFor(condition, timeout_ms);
  }
};

TEST_F(DBusTransactionServiceCalls, ProductionTimeoutsAreThirtySecondsOneHourAndTheBusDefault) {
  const DBusTransactionService::Timeouts defaults;
  EXPECT_EQ(defaults.prepare_ms, 30'000);
  EXPECT_EQ(defaults.execute_ms, 3'600'000);
  EXPECT_EQ(defaults.cancel_ms, -1);
  const auto service = makeService();
  EXPECT_EQ(service->timeouts().prepare_ms, 30'000);
  EXPECT_EQ(service->timeouts().execute_ms, 3'600'000);
  EXPECT_EQ(service->timeouts().cancel_ms, -1);
  EXPECT_NE(service->timeouts().cancel_ms, service->timeouts().execute_ms);
}

TEST_F(DBusTransactionServiceCalls, PrepareExecuteAndCancelSendOnlyTheDeclaredArguments) {
  FakeHelperObject& fake = startFakeHelper();
  fake.script(QStringLiteral("PreparePlan"), {.mode = Mode::Immediate, .delay_ms = 0, .reply = preparedReply()});
  helper_wire::ResultWire result;
  result.outcome = QStringLiteral("Succeeded");
  fake.script(QStringLiteral("Execute"),
              {.mode = Mode::Immediate, .delay_ms = 0, .reply = {QVariant::fromValue(result)}});
  fake.script(QStringLiteral("Cancel"),
              {.mode = Mode::Immediate, .delay_ms = 0, .reply = {QVariant::fromValue(helper_wire::StatusWire{})}});
  const auto service = makeService();

  Captured<PrepareReply> prepared;
  service->preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}},
                       [&](PrepareReply reply) { prepared.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return prepared.done(); }));
  ASSERT_TRUE(prepared.reply->has_value());
  EXPECT_EQ((*prepared.reply)->plan_id, "plan-1");

  Captured<ExecuteReply> executed;
  service->execute("plan-1", [&](ExecuteReply reply) { executed.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return executed.done(); }));
  ASSERT_TRUE(executed.reply->has_value());
  EXPECT_EQ((*executed.reply)->outcome, ExecutionOutcome::Succeeded);

  Captured<CancelReply> cancelled;
  service->cancel("plan-1", [&](CancelReply reply) { cancelled.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return cancelled.done(); }));
  EXPECT_TRUE(cancelled.reply->has_value());

  EXPECT_TRUE(fake.rejected().isEmpty());
  ASSERT_EQ(fake.argumentsOf(QStringLiteral("PreparePlan")).size(), 1);
  const QVariantList sent = fake.argumentsOf(QStringLiteral("PreparePlan")).front();
  ASSERT_EQ(sent.size(), 3);
  EXPECT_EQ(sent.at(0).toString(), QStringLiteral("install"));
  EXPECT_EQ(sent.at(1).toStringList(), QStringList{QStringLiteral("app")});
}

TEST_F(DBusTransactionServiceCalls, AnInjectedShortExecuteTimeoutReportsNoReplyWithOneExecuteRecorded) {
  FakeHelperObject& fake = startFakeHelper();
  fake.script(QStringLiteral("Execute"), {.mode = Mode::Never, .delay_ms = 0, .reply = {}});
  const auto service = makeService({.prepare_ms = 30'000, .execute_ms = 300, .cancel_ms = -1});

  Captured<ExecuteReply> executed;
  service->execute("plan-1", [&](ExecuteReply reply) { executed.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return executed.done(); }));
  ASSERT_FALSE(executed.reply->has_value());
  EXPECT_EQ(executed.reply->error().kind, ServiceError::Kind::NoReply);
  EXPECT_EQ(fake.calls(QStringLiteral("Execute")), 1);
  QTest::qWait(200);
  EXPECT_EQ(fake.calls(QStringLiteral("Execute")), 1) << "the adapter never resends Execute";
  EXPECT_EQ(fake.calls(QStringLiteral("PreparePlan")), 0);
}

TEST_F(DBusTransactionServiceCalls, ExecuteReportsNoReplyWhenTheHelperDiesMidCall) {
  FakeHelperObject& fake = startFakeHelper();
  fake.script(QStringLiteral("Execute"), {.mode = Mode::Never, .delay_ms = 0, .reply = {}});
  const auto service = makeService();

  Captured<ExecuteReply> executed;
  service->execute("plan-1", [&](ExecuteReply reply) { executed.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return fake.calls(QStringLiteral("Execute")) == 1; }));
  fake_.reset();
  helper_connection_.reset();
  QDBusConnection::disconnectFromBus(helper_name_);
  ASSERT_TRUE(waitFor([&] { return executed.done(); }));
  ASSERT_FALSE(executed.reply->has_value());
  EXPECT_EQ(executed.reply->error().kind, ServiceError::Kind::NoReply);
}

TEST_F(DBusTransactionServiceCalls, CallsDoNotBlockTheEventLoop) {
  FakeHelperObject& fake = startFakeHelper();
  fake.script(QStringLiteral("PreparePlan"), {.mode = Mode::Delayed, .delay_ms = 2000, .reply = preparedReply()});
  const auto service = makeService();

  int ticks = 0;
  QTimer ticker;
  ticker.setInterval(100);
  QObject::connect(&ticker, &QTimer::timeout, [&ticks] { ++ticks; });
  ticker.start();

  Captured<PrepareReply> prepared;
  service->preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}},
                       [&](PrepareReply reply) { prepared.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return prepared.done(); }, 6000));
  EXPECT_GE(ticks, 10) << "the event loop kept running while the call waited";
  EXPECT_TRUE(prepared.reply->has_value());
}

TEST_F(DBusTransactionServiceCalls, ACallToANameWithNoOwnerReportsHelperUnavailable) {
  const auto service = makeService();
  Captured<PrepareReply> prepared;
  service->preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}},
                       [&](PrepareReply reply) { prepared.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return prepared.done(); }));
  ASSERT_FALSE(prepared.reply->has_value());
  EXPECT_EQ(prepared.reply->error().kind, ServiceError::Kind::HelperUnavailable);
}

TEST_F(DBusTransactionServiceCalls, AFactoryWithoutAConnectionReportsHelperUnavailableAsynchronously) {
  DBusTransactionService service([] { return QDBusConnection(QStringLiteral("not-connected")); });
  Captured<PrepareReply> prepared;
  bool called_inside = true;
  service.preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}},
                      [&](PrepareReply reply) { prepared.reply = std::move(reply); });
  called_inside = prepared.done();
  EXPECT_FALSE(called_inside) << "never synchronous";
  ASSERT_TRUE(waitFor([&] { return prepared.done(); }));
  EXPECT_EQ(prepared.reply->error().kind, ServiceError::Kind::HelperUnavailable);
}

TEST_F(DBusTransactionServiceCalls, DomainRefusalsAreNeverReportedAsHelperUnavailable) {
  FakeHelperObject& fake = startFakeHelper();
  helper_wire::StatusWire status;
  status.code = QStringLiteral("NotAuthorized");
  helper_wire::ResultWire result;
  result.outcome = QStringLiteral("Failed");
  result.status = status;
  fake.script(QStringLiteral("Execute"),
              {.mode = Mode::Immediate, .delay_ms = 0, .reply = {QVariant::fromValue(result)}});
  const auto service = makeService();
  Captured<ExecuteReply> executed;
  service->execute("plan-1", [&](ExecuteReply reply) { executed.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return executed.done(); }));
  ASSERT_TRUE(executed.reply->has_value());
  EXPECT_EQ((*executed.reply)->failure.code, TransactionErrorCode::NotAuthorized);
}

TEST_F(DBusTransactionServiceCalls, AnUnknownCodeBecomesInternalErrorWithNoNames) {
  FakeHelperObject& fake = startFakeHelper();
  helper_wire::StatusWire status;
  status.code = QStringLiteral("SomethingElse");
  status.names = {QStringLiteral("secret")};
  fake.script(QStringLiteral("PreparePlan"),
              {
                  .mode = Mode::Immediate,
                  .delay_ms = 0,
                  .reply = {QVariant::fromValue(status), QString(), QVariant::fromValue(helper_wire::PreviewWire{})},
              });
  const auto service = makeService();
  Captured<PrepareReply> prepared;
  service->preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}},
                       [&](PrepareReply reply) { prepared.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return prepared.done(); }));
  ASSERT_FALSE(prepared.reply->has_value());
  EXPECT_EQ(prepared.reply->error().failure.code, TransactionErrorCode::InternalError);
  EXPECT_TRUE(prepared.reply->error().failure.names.empty());
}

TEST_F(DBusTransactionServiceCalls, AllTwentyFourCodesDecodeToThemselves) {
  FakeHelperObject& fake = startFakeHelper();
  const auto service = makeService();
  for (std::size_t index = 0; index < kTransactionErrorCodeCount; ++index) {
    const auto code = static_cast<TransactionErrorCode>(index);
    helper_wire::StatusWire status;
    status.code = QString::fromLatin1(transactionErrorName(code).data(),
                                      static_cast<qsizetype>(transactionErrorName(code).size()));
    fake.script(QStringLiteral("Cancel"),
                {.mode = Mode::Immediate, .delay_ms = 0, .reply = {QVariant::fromValue(status)}});
    Captured<CancelReply> cancelled;
    service->cancel("plan-1", [&](CancelReply reply) { cancelled.reply = std::move(reply); });
    ASSERT_TRUE(waitFor([&] { return cancelled.done(); }));
    ASSERT_FALSE(cancelled.reply->has_value());
    EXPECT_EQ(cancelled.reply->error().failure.code, code);
  }
}

TEST_F(DBusTransactionServiceCalls, AnUndecodableReplyIsNoReplyNotAFailure) {
  FakeHelperObject& fake = startFakeHelper();
  fake.script(QStringLiteral("Cancel"),
              {.mode = Mode::Immediate, .delay_ms = 0, .reply = {QStringLiteral("not a status")}});
  const auto service = makeService();
  Captured<CancelReply> cancelled;
  service->cancel("plan-1", [&](CancelReply reply) { cancelled.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return cancelled.done(); }));
  ASSERT_FALSE(cancelled.reply->has_value());
  EXPECT_EQ(cancelled.reply->error().kind, ServiceError::Kind::NoReply) << "an undecodable reply cannot be trusted";
}

}  // namespace

namespace {

class DBusTransactionServiceProgress : public DBusTransactionServiceCalls {
 protected:
  struct Seen {
    std::vector<std::pair<std::string, ProgressUpdate>> events;
  };

  // Prepares a plan through the adapter (so it subscribes) and returns the adapter.
  std::unique_ptr<DBusTransactionService> preparedAdapter(FakeHelperObject& fake, Seen& seen) {
    fake.script(QStringLiteral("PreparePlan"), {.mode = Mode::Immediate, .delay_ms = 0, .reply = preparedReply()});
    auto service = makeService();
    service->setProgressHandler([&seen](const std::string& plan_id, const ProgressUpdate& update) {
      seen.events.emplace_back(plan_id, update);
    });
    Captured<PrepareReply> prepared;
    service->preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}},
                         [&](PrepareReply reply) { prepared.reply = std::move(reply); });
    EXPECT_TRUE(waitFor([&] { return prepared.done(); }));
    return service;
  }
};

TEST_F(DBusTransactionServiceProgress, MatchRuleNamesTheInterfaceTheMemberAndTheHelpersWellKnownName) {
  const QString rule = DBusTransactionService::progressMatchRule();
  EXPECT_TRUE(rule.contains(QStringLiteral("interface='org.holonight.PackageHelper1.Transactions'")));
  EXPECT_TRUE(rule.contains(QStringLiteral("member='Progress'")));
  EXPECT_TRUE(rule.contains(QStringLiteral("sender='org.holonight.PackageHelper1'")));
}

TEST_F(DBusTransactionServiceProgress, TheHelpersEventsForTheCurrentPlanArriveInOrder) {
  FakeHelperObject& fake = startFakeHelper();
  Seen seen;
  const auto service = preparedAdapter(fake, seen);
  fake.emitProgress(QStringLiteral("plan-1"), QStringLiteral("Downloading"), 1, 2, QStringLiteral("foo"));
  fake.emitProgress(QStringLiteral("plan-1"), QStringLiteral("Installing"), 1, 1, QStringLiteral("bar"));
  ASSERT_TRUE(waitFor([&] { return seen.events.size() == 2; }));
  EXPECT_EQ(seen.events[0].first, "plan-1");
  EXPECT_EQ(seen.events[0].second.phase, ProgressPhase::Downloading);
  EXPECT_EQ(seen.events[0].second.item, "foo");
  EXPECT_EQ(seen.events[1].second.phase, ProgressPhase::Installing);
}

TEST_F(DBusTransactionServiceProgress, AnUnrelatedConnectionEmittingProgressForTheSamePlanIsIgnored) {
  FakeHelperObject& fake = startFakeHelper();
  Seen seen;
  const auto service = preparedAdapter(fake, seen);
  QDBusConnection intruder = connect(QStringLiteral("intruder"));
  QDBusMessage forged = QDBusMessage::createSignal(QStringLiteral("/org/holonight/PackageHelper1"),
                                                   QStringLiteral("org.holonight.PackageHelper1.Transactions"),
                                                   QStringLiteral("Progress"));
  forged << QStringLiteral("plan-1") << QStringLiteral("Downloading") << 5U << 5U << QStringLiteral("evil");
  ASSERT_TRUE(intruder.send(forged));
  fake.emitProgress(QStringLiteral("plan-1"), QStringLiteral("Verifying"), 0, 0, QStringLiteral("real"));
  ASSERT_TRUE(waitFor([&] { return !seen.events.empty(); }));
  QTest::qWait(200);
  ASSERT_EQ(seen.events.size(), 1U) << "only the helper's own emission arrives";
  EXPECT_EQ(seen.events[0].second.item, "real");
}

TEST_F(DBusTransactionServiceProgress, AnEventForAnotherPlanIsDropped) {
  FakeHelperObject& fake = startFakeHelper();
  Seen seen;
  const auto service = preparedAdapter(fake, seen);
  fake.emitProgress(QStringLiteral("someone-elses-plan"), QStringLiteral("Downloading"), 1, 2, QStringLiteral("x"));
  fake.emitProgress(QStringLiteral("plan-1"), QStringLiteral("Dancing"), 1, 2, QStringLiteral("unknown phase"));
  fake.emitProgress(QStringLiteral("plan-1"), QStringLiteral("Done"), 1, 1, QString());
  ASSERT_TRUE(waitFor([&] { return !seen.events.empty(); }));
  QTest::qWait(200);
  ASSERT_EQ(seen.events.size(), 1U);
  EXPECT_EQ(seen.events[0].second.phase, ProgressPhase::Done);
}

}  // namespace
