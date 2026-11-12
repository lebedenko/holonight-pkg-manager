#include "DBusTransactionService.h"
#include "helper_bus_fixture.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QXmlStreamReader>

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

// system-upgrade REQ-F-080: the sync of a preparation reports with an empty plan ID, before any plan exists.
TEST_F(DBusTransactionServiceProgress, SyncingWithAnEmptyPlanIdArrivesWhileAnUpgradeIsPrepared) {
  FakeHelperObject& fake = startFakeHelper();
  fake.script(QStringLiteral("PrepareUpgrade"), {.mode = Mode::Never, .delay_ms = 0, .reply = {}});
  Seen seen;
  auto service = makeService();
  service->setProgressHandler(
      [&seen](const std::string& plan_id, const ProgressUpdate& update) { seen.events.emplace_back(plan_id, update); });
  service->preparePlan(PlanRequest{.action = PlanAction::Upgrade, .names = {}}, [](PrepareReply) {});
  ASSERT_TRUE(waitFor([&] { return fake.calls(QStringLiteral("PrepareUpgrade")) == 1; }));
  fake.emitProgress(QString(), QStringLiteral("Syncing"), 1, 2, QStringLiteral("core"));
  ASSERT_TRUE(waitFor([&] { return !seen.events.empty(); }));
  EXPECT_EQ(seen.events.front().first, "");
  EXPECT_EQ(seen.events.front().second.phase, ProgressPhase::Syncing);
  EXPECT_EQ(seen.events.front().second.item, "core");
}

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

// ---- system-upgrade T-065: PrepareUpgrade, its preview, the probe and the version 1 contract ----

helper_wire::UpgradePreviewWire upgradePreview() {
  helper_wire::UpgradePreviewWire preview;
  preview.upgrades.push_back({.name = QStringLiteral("app"),
                              .old_version = QStringLiteral("1"),
                              .new_version = QStringLiteral("2"),
                              .repository = QStringLiteral("core")});
  preview.replacements.push_back({.old_name = QStringLiteral("old"),
                                  .old_version = QStringLiteral("1"),
                                  .new_name = QStringLiteral("new"),
                                  .new_version = QStringLiteral("3"),
                                  .repository = QStringLiteral("extra")});
  preview.new_dependencies.push_back(
      {.name = QStringLiteral("dep"), .version = QStringLiteral("4"), .repository = QStringLiteral("core")});
  preview.held_back.push_back({.name = QStringLiteral("kept"), .installed_version = QStringLiteral("5")});
  preview.keyring = true;
  preview.keyring_packages = {QStringLiteral("archlinux-keyring")};
  preview.download_bytes = 1234;
  preview.size_change_bytes = -56;
  preview.expires_at = 1700000300;
  return preview;
}

TEST_F(DBusTransactionServiceCalls, PrepareUpgradeSendsAnEmptyOptionsMapWithATenMinuteTimeout) {
  EXPECT_EQ(DBusTransactionService::Timeouts{}.upgrade_prepare_ms, 600'000);
  FakeHelperObject& fake = startFakeHelper();
  fake.script(QStringLiteral("PrepareUpgrade"),
              {.mode = Mode::Immediate,
               .delay_ms = 0,
               .reply = {QVariant::fromValue(helper_wire::StatusWire{}), QStringLiteral("plan-u"),
                         QVariant::fromValue(upgradePreview())}});
  const auto service = makeService();
  Captured<PrepareReply> prepared;
  service->preparePlan(PlanRequest{.action = PlanAction::Upgrade, .names = {}},
                       [&](PrepareReply reply) { prepared.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return prepared.done(); }));
  EXPECT_TRUE(fake.rejected().isEmpty());
  EXPECT_EQ(fake.calls(QStringLiteral("PreparePlan")), 0);
  ASSERT_EQ(fake.argumentsOf(QStringLiteral("PrepareUpgrade")).size(), 1);
  const QVariantList sent = fake.argumentsOf(QStringLiteral("PrepareUpgrade")).front();
  ASSERT_EQ(sent.size(), 1);
  EXPECT_TRUE(qdbus_cast<QVariantMap>(sent.at(0)).isEmpty());

  ASSERT_TRUE(prepared.reply->has_value()) << static_cast<int>(prepared.reply->error().kind);
  const PreparedPlan& plan = **prepared.reply;
  EXPECT_EQ(plan.plan_id, "plan-u");
  ASSERT_TRUE(plan.plan.upgrade.has_value());
  const UpgradeDetail& detail = *plan.plan.upgrade;
  ASSERT_EQ(detail.upgrades.size(), 1U);
  EXPECT_EQ(detail.upgrades[0].new_version, "2");
  ASSERT_EQ(detail.replacements.size(), 1U);
  EXPECT_EQ(detail.replacements[0].old_name, "old");
  ASSERT_EQ(detail.new_dependencies.size(), 1U);
  ASSERT_EQ(detail.held_back.size(), 1U);
  EXPECT_EQ(detail.keyring_packages, std::vector<std::string>{"archlinux-keyring"});
  EXPECT_EQ(detail.download_bytes, 1234U);
  EXPECT_EQ(detail.size_change_bytes, -56);
}

TEST_F(DBusTransactionServiceCalls, AnUpgradeRefusalIsADomainRefusal) {
  FakeHelperObject& fake = startFakeHelper();
  helper_wire::StatusWire status;
  status.code = QStringLiteral("NothingToDo");
  fake.script(QStringLiteral("PrepareUpgrade"), {.mode = Mode::Immediate,
                                                 .delay_ms = 0,
                                                 .reply = {QVariant::fromValue(status), QString(),
                                                           QVariant::fromValue(helper_wire::UpgradePreviewWire{})}});
  const auto service = makeService();
  Captured<PrepareReply> prepared;
  service->preparePlan(PlanRequest{.action = PlanAction::Upgrade, .names = {}},
                       [&](PrepareReply reply) { prepared.reply = std::move(reply); });
  ASSERT_TRUE(waitFor([&] { return prepared.done(); }));
  ASSERT_FALSE(prepared.reply->has_value());
  EXPECT_EQ(prepared.reply->error().kind, ServiceError::Kind::Refused);
  EXPECT_EQ(prepared.reply->error().failure.code, TransactionErrorCode::NothingToDo);
}

TEST_F(DBusTransactionServiceCalls, TheProbeReadsTheProtocolVersion) {
  FakeHelperObject& fake = startFakeHelper();
  fake.setProtocolVersion(1);
  const auto service = makeService();
  std::optional<VersionReply> version;
  service->queryProtocolVersion(ProbeMode::NoAutoStart, [&](VersionReply reply) { version = reply; });
  ASSERT_TRUE(waitFor([&] { return version.has_value(); }));
  ASSERT_TRUE(version->has_value());
  EXPECT_EQ(version->value(), 1U);
  EXPECT_EQ(fake.propertyReads(), 1);
}

TEST_F(DBusTransactionServiceCalls, AProbeWithoutAutoStartOfAMissingHelperIsHelperUnavailable) {
  const auto service = makeService();
  std::optional<VersionReply> version;
  service->queryProtocolVersion(ProbeMode::NoAutoStart, [&](VersionReply reply) { version = reply; });
  ASSERT_TRUE(waitFor([&] { return version.has_value(); }));
  ASSERT_FALSE(version->has_value());
  EXPECT_EQ(version->error().kind, ServiceError::Kind::HelperUnavailable);
}

TEST(HelperProtocol, VersionTwoKeepsEveryMemberOfVersionOneUnchanged) {
  QFile golden(QString::fromLatin1(HOLONIGHT_PROTOCOL_V1_SIGNATURES));
  ASSERT_TRUE(golden.open(QIODevice::ReadOnly | QIODevice::Text));
  QFile xmlFile(QString::fromLatin1(HOLONIGHT_HELPER_DBUS_XML));
  ASSERT_TRUE(xmlFile.open(QIODevice::ReadOnly));
  QSet<QString> members;
  QXmlStreamReader xml(&xmlFile);
  QString owner;
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) {
      continue;
    }
    const auto attributes = xml.attributes();
    const QString name = attributes.value(QLatin1String("name")).toString();
    if (xml.name() == QLatin1String("method") || xml.name() == QLatin1String("signal")) {
      owner = xml.name().toString() + QLatin1Char(' ') + name;
    } else if (xml.name() == QLatin1String("arg")) {
      const QString direction = attributes.value(QLatin1String("direction")).toString();
      members.insert(owner + (direction.isEmpty() ? QString() : QLatin1Char(' ') + direction) + QLatin1Char(' ') +
                     name + QLatin1Char(' ') + attributes.value(QLatin1String("type")).toString());
    } else if (xml.name() == QLatin1String("property")) {
      members.insert(QStringLiteral("property %1 %2 %3")
                         .arg(name, attributes.value(QLatin1String("type")).toString(),
                              attributes.value(QLatin1String("access")).toString()));
    }
  }
  int lines = 0;
  while (!golden.atEnd()) {
    const QString line = QString::fromUtf8(golden.readLine()).trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
      continue;
    }
    ++lines;
    EXPECT_TRUE(members.contains(line)) << line.toStdString();
  }
  EXPECT_EQ(lines, 17);
}

}  // namespace
