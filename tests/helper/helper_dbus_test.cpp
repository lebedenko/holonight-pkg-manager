#include "DBusCallerIdentity.h"
#include "HelperIdentity.h"
#include "HelperService.h"
#include "QtWorkRunner.h"
#include "WireTypes.h"
#include "fake_authorizer.h"
#include "fake_plan_id_generator.h"
#include "fake_transaction_engine.h"
#include "holonight_packages_application/update_check_runtime.h"
#include "private_bus.h"
#include "recording_terminator.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QXmlStreamReader>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mutex>

namespace holonight_helper {
namespace {

using holonight_packages_application::AuthorizationDecision;
using holonight_packages_application::PlanService;
using holonight_packages_application::SystemClock;
using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlannedChange;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_testing::FakeAuthorizer;
using holonight_packages_testing::FakePlanIdGenerator;
using holonight_packages_testing::FakeTransactionEngine;
using holonight_packages_testing::PrivateBus;
using holonight_packages_testing::RecordingTerminator;
using ::testing::ElementsAre;

const QString kService = QString::fromLatin1(kBusName);
const QString kPath = QString::fromLatin1(kObjectPath);
const QString kInterface = QString::fromLatin1(kInterfaceName);

// Receives Progress signals on one connection.
class ProgressRecorder : public QObject {
  Q_OBJECT

 public:
  QList<QStringList> received;

 public slots:
  void onProgress(const QString& planId, const QString& phase, uint current, uint total, const QString& item) {
    received.push_back({planId, phase, QString::number(current), QString::number(total), item});
  }
};

// Interface members as comparable strings, independent of document order.
QSet<QString> membersOf(const QByteArray& xml) {
  QSet<QString> members;
  QXmlStreamReader reader(xml);
  bool inInterface = false;
  QString method;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      const QString name = reader.name().toString();
      const auto attributes = reader.attributes();
      if (name == QLatin1String("interface")) {
        inInterface = attributes.value(QLatin1String("name")) == kInterface;
      } else if (inInterface && (name == QLatin1String("method") || name == QLatin1String("signal"))) {
        method = name + QLatin1Char(' ') + attributes.value(QLatin1String("name")).toString();
        members.insert(method);
      } else if (inInterface && name == QLatin1String("arg")) {
        members.insert(method + QStringLiteral(" %1 %2 %3")
                                    .arg(attributes.value(QLatin1String("direction")).isEmpty()
                                             ? QStringLiteral("signal")
                                             : attributes.value(QLatin1String("direction")).toString(),
                                         attributes.value(QLatin1String("name")).toString(),
                                         attributes.value(QLatin1String("type")).toString()));
      } else if (inInterface && name == QLatin1String("property")) {
        members.insert(QStringLiteral("property %1 %2 %3")
                           .arg(attributes.value(QLatin1String("name")).toString(),
                                attributes.value(QLatin1String("type")).toString(),
                                attributes.value(QLatin1String("access")).toString()));
      }
    } else if (reader.isEndElement() && reader.name() == QLatin1String("method")) {
      method.clear();
    }
  }
  return members;
}

QDBusMessage await(const QDBusPendingCall& pending, int milliseconds = 10000) {
  QDBusPendingCallWatcher watcher(pending);
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(milliseconds);
  if (!pending.isFinished()) {
    loop.exec();
  }
  return pending.reply();
}

void pump(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

class HelperBusTest : public ::testing::Test {
 public:
  void SetUp() override {
    registerWireTypes();
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    static std::atomic<int> counter{0};
    const QString suffix = QString::number(counter++);
    server_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("helper-server-") + suffix);
    client_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("helper-client-") + suffix);
    other_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("helper-other-") + suffix);
    ASSERT_TRUE(server_.isConnected() && client_.isConnected() && other_.isConnected());
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
    };
    engine_.resolveResult = plan;
    identities_ = std::make_unique<DBusCallerIdentity>(server_);
    plans_ = std::make_unique<PlanService>(PlanService::Dependencies{
        .engine = engine_,
        .authorizer = authorizer_,
        .identities = *identities_,
        .ids = ids_,
        .clock = clock_,
        .runner = runner_,
        .terminator = terminator_,
    });
    service_ = std::make_unique<HelperService>(server_, *plans_);
    ASSERT_TRUE(service_->start());
  }

  void TearDown() override {
    service_.reset();
    plans_.reset();
    for (const char* name : {"server", "client", "other"}) {
      Q_UNUSED(name)
    }
    QDBusConnection::disconnectFromBus(server_.name());
    QDBusConnection::disconnectFromBus(client_.name());
    QDBusConnection::disconnectFromBus(other_.name());
  }

  static QDBusPendingCall start(QDBusConnection& from, const QString& method, const QVariantList& arguments) {
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    message.setArguments(arguments);
    return from.asyncCall(message, 30000);
  }
  static QDBusMessage call(QDBusConnection& from, const QString& method, const QVariantList& arguments) {
    return await(start(from, method, arguments));
  }

  struct Prepared {
    StatusWire status;
    QString id;
    PreviewWire preview;
  };
  static Prepared prepare(QDBusConnection& from, const QString& action = QStringLiteral("install"),
                          const QStringList& names = {QStringLiteral("app")}) {
    const QDBusMessage reply = call(from, QStringLiteral("PreparePlan"), {action, names, QVariantMap()});
    Prepared prepared;
    if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() == 3) {
      prepared.status = qdbus_cast<StatusWire>(reply.arguments().at(0));
      prepared.id = reply.arguments().at(1).toString();
      prepared.preview = qdbus_cast<PreviewWire>(reply.arguments().at(2));
    } else {
      ADD_FAILURE() << reply.errorName().toStdString() << ": " << reply.errorMessage().toStdString();
    }
    return prepared;
  }
  static ResultWire execute(QDBusConnection& from, const QString& plan_id) {
    const QDBusMessage reply = call(from, QStringLiteral("Execute"), {plan_id});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
      ADD_FAILURE() << reply.errorName().toStdString();
      return {};
    }
    return qdbus_cast<ResultWire>(reply.arguments().at(0));
  }

  PrivateBus bus_;
  QDBusConnection server_{QStringLiteral("unused")};
  QDBusConnection client_{QStringLiteral("unused")};
  QDBusConnection other_{QStringLiteral("unused")};
  FakeTransactionEngine engine_;
  FakeAuthorizer authorizer_;
  FakePlanIdGenerator ids_;
  SystemClock clock_;
  QtWorkRunner runner_;
  RecordingTerminator terminator_;
  std::unique_ptr<DBusCallerIdentity> identities_;
  std::unique_ptr<PlanService> plans_;
  std::unique_ptr<HelperService> service_;
};

TEST_F(HelperBusTest, TheIntrospectionAnswersWithTheMembersOfTheInstalledXml) {
  QDBusMessage introspect = QDBusMessage::createMethodCall(
      kService, kPath, QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
  const QDBusMessage reply = await(client_.asyncCall(introspect));
  ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  QFile file(QString::fromLatin1(HOLONIGHT_HELPER_DBUS_XML));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(membersOf(reply.arguments().at(0).toString().toUtf8()), membersOf(file.readAll()));
}

TEST_F(HelperBusTest, ExecuteDeclaresExactlyOneInputOfTypeStringAndNothingNamesAPath) {
  QFile file(QString::fromLatin1(HOLONIGHT_HELPER_DBUS_XML));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const QSet<QString> members = membersOf(file.readAll());
  EXPECT_TRUE(members.contains(QStringLiteral("method Execute in plan_id s")));
  int inputs = 0;
  for (const QString& member : members) {
    if (member.startsWith(QStringLiteral("method Execute in "))) {
      ++inputs;
    }
    for (const char* forbidden : {"path", "repo", "server", "config"}) {
      EXPECT_FALSE(member.contains(QLatin1String(forbidden), Qt::CaseInsensitive)) << member.toStdString();
    }
  }
  EXPECT_EQ(inputs, 1);
}

TEST_F(HelperBusTest, PreparingNeedsNoAuthorizationAndReturnsAPlanIdAndPreview) {
  authorizer_.answer = AuthorizationDecision::NotAuthorized;
  const Prepared prepared = prepare(client_);
  EXPECT_TRUE(prepared.status.code.isEmpty());
  EXPECT_EQ(prepared.id.size(), 32);
  ASSERT_EQ(prepared.preview.targets.size(), 1);
  EXPECT_EQ(prepared.preview.targets[0].name, QStringLiteral("app"));
  EXPECT_EQ(prepared.preview.targets[0].repository, QStringLiteral("core"));
  EXPECT_GT(prepared.preview.expires_at, 0);
  EXPECT_THAT(authorizer_.requests(), ::testing::IsEmpty());
}

TEST_F(HelperBusTest, RefusalsAreNormalRepliesWithACodeAndNoPlan) {
  engine_.resolveResult = std::unexpected(
      TransactionFailure{.code = TransactionErrorCode::PackageNotFound, .names = {"app"}, .choices = {}});
  const Prepared missing = prepare(client_);
  EXPECT_EQ(missing.status.code, QStringLiteral("PackageNotFound"));
  EXPECT_EQ(missing.status.names, QStringList{QStringLiteral("app")});
  EXPECT_TRUE(missing.id.isEmpty());
  const Prepared invalid = prepare(client_, QStringLiteral("upgrade"));
  EXPECT_EQ(invalid.status.code, QStringLiteral("InvalidArgument"));
  const QDBusMessage withOption = call(client_, QStringLiteral("PreparePlan"),
                                       {
                                           QStringLiteral("install"),
                                           QStringList{QStringLiteral("app")},
                                           QVariantMap{{QStringLiteral("noconfirm"), true}},
                                       });
  EXPECT_EQ(qdbus_cast<StatusWire>(withOption.arguments().at(0)).code, QStringLiteral("InvalidArgument"));
}

TEST_F(HelperBusTest, ExecuteRunsTheApprovedPlanAndRepliesWithAStructuredResult) {
  engine_.executeResult = ExecutionResult::succeeded({
      holonight_packages_domain::ChangedPackage{
          .name = "app",
          .action = holonight_packages_domain::ChangeAction::Install,
          .old_version = "",
          .new_version = "1.0-1",
      },
  });
  const Prepared prepared = prepare(client_);
  const ResultWire result = execute(client_, prepared.id);
  EXPECT_EQ(result.outcome, QStringLiteral("Succeeded"));
  ASSERT_EQ(result.changed.size(), 1);
  EXPECT_EQ(result.changed[0].name, QStringLiteral("app"));
  ASSERT_EQ(authorizer_.requests().size(), 1U);
  EXPECT_EQ(authorizer_.requests()[0].action_id, "org.holonight.packages.install");
  EXPECT_EQ(authorizer_.requests()[0].bus_name, client_.baseService().toStdString());
  EXPECT_EQ(execute(client_, prepared.id).status.code, QStringLiteral("PlanNotFound"));
}

TEST_F(HelperBusTest, AnExtraArgumentToExecuteIsRejectedByTheBusLayerAndTheNeedPlanSurvives) {
  const Prepared prepared = prepare(client_);
  const QDBusMessage rejected = call(client_, QStringLiteral("Execute"), {prepared.id, QStringLiteral("extra")});
  EXPECT_EQ(rejected.type(), QDBusMessage::ErrorMessage);
  EXPECT_EQ(engine_.executeCalls(), 0);
  EXPECT_EQ(execute(client_, prepared.id).outcome, QStringLiteral("Succeeded"));
}

TEST_F(HelperBusTest, OnlyTheOwnerCanExecuteOrCancelAndTheRepliesDoNotRevealOtherPlans) {
  const Prepared prepared = prepare(client_);
  const ResultWire foreign = execute(other_, prepared.id);
  const ResultWire unknown = execute(other_, QStringLiteral("00000000000000000000000000000fff"));
  EXPECT_EQ(foreign.status.code, QStringLiteral("PlanNotFound"));
  EXPECT_EQ(foreign.status.code, unknown.status.code);
  const QDBusMessage cancelled = call(other_, QStringLiteral("Cancel"), {prepared.id});
  EXPECT_EQ(qdbus_cast<StatusWire>(cancelled.arguments().at(0)).code, QStringLiteral("PlanNotFound"));
  EXPECT_EQ(engine_.executeCalls(), 0);
  EXPECT_EQ(execute(client_, prepared.id).outcome, QStringLiteral("Succeeded"));
}

TEST_F(HelperBusTest, WhileAnExecuteIsBlockedCancelPropertiesAndPrepareStillAnswerPromptly) {
  std::mutex mutex;
  std::condition_variable condition;
  bool release = false;
  std::atomic<bool> started{false};
  engine_.executeScript = [&](const auto&, const auto&, const ExecutionContext&) {
    started = true;
    std::unique_lock lock(mutex);
    condition.wait_for(lock, std::chrono::seconds(20), [&] { return release; });
    return ExecutionResult::succeeded();
  };
  const Prepared prepared = prepare(client_);
  QDBusPendingCall running = start(client_, QStringLiteral("Execute"), {prepared.id});
  for (int i = 0; i < 200 && !started; ++i) {
    pump(10);
  }
  ASSERT_TRUE(started);

  const auto timeIt = [](auto&& action) {
    const auto begin = std::chrono::steady_clock::now();
    action();
    return std::chrono::steady_clock::now() - begin;
  };
  QDBusMessage busyProperty;
  EXPECT_LT(timeIt([&] { busyProperty = call(other_, QStringLiteral("org.freedesktop.DBus.Properties.Get"), {}); }),
            std::chrono::milliseconds(100));
  QDBusMessage get = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                    QStringLiteral("Get"));
  get << kInterface << QStringLiteral("Busy");
  QDBusMessage busy;
  EXPECT_LT(timeIt([&] { busy = await(other_.asyncCall(get)); }), std::chrono::milliseconds(100));
  ASSERT_EQ(busy.type(), QDBusMessage::ReplyMessage);
  EXPECT_TRUE(busy.arguments().at(0).value<QDBusVariant>().variant().toBool());

  Prepared second;
  EXPECT_LT(timeIt([&] { second = prepare(other_); }), std::chrono::milliseconds(100));
  EXPECT_EQ(second.status.code, QStringLiteral("Busy"));

  // Cancel is accepted; the Execute call gets its Cancelled reply first and the helper is then asked to end.
  QDBusMessage cancel;
  EXPECT_LT(timeIt([&] { cancel = call(client_, QStringLiteral("Cancel"), {prepared.id}); }),
            std::chrono::milliseconds(100));
  EXPECT_TRUE(qdbus_cast<StatusWire>(cancel.arguments().at(0)).code.isEmpty());
  const QDBusMessage reply = await(running);
  ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  EXPECT_EQ(qdbus_cast<ResultWire>(reply.arguments().at(0)).outcome, QStringLiteral("Cancelled"));
  pump(50);
  EXPECT_EQ(terminator_.calls(), 1);
  {
    const std::scoped_lock lock(mutex);
    release = true;
  }
  condition.notify_all();
  pump(100);
}

TEST_F(HelperBusTest, ProgressGoesOnlyToTheOwnersUniqueName) {
  ProgressRecorder owner;
  ProgressRecorder bystander;
  ASSERT_TRUE(client_.connect(kService, kPath, kInterface, QStringLiteral("Progress"), &owner,
                              SLOT(onProgress(QString, QString, uint, uint, QString))));
  ASSERT_TRUE(other_.connect(kService, kPath, kInterface, QStringLiteral("Progress"), &bystander,
                             SLOT(onProgress(QString, QString, uint, uint, QString))));
  engine_.executeScript = [](const auto&, const auto&, const ExecutionContext& context) {
    context.progress(ProgressUpdate{.phase = ProgressPhase::Downloading, .current = 1, .total = 2, .item = "app"});
    context.progress(ProgressUpdate{.phase = ProgressPhase::Done, .current = 1, .total = 1, .item = {}});
    return ExecutionResult::succeeded();
  };
  const Prepared prepared = prepare(client_);
  EXPECT_EQ(execute(client_, prepared.id).outcome, QStringLiteral("Succeeded"));
  pump(200);
  ASSERT_EQ(owner.received.size(), 2);
  EXPECT_EQ(owner.received[0], (QStringList{prepared.id, QStringLiteral("Downloading"), QStringLiteral("1"),
                                            QStringLiteral("2"), QStringLiteral("app")}));
  EXPECT_EQ(owner.received[1][1], QStringLiteral("Done"));
  EXPECT_TRUE(bystander.received.isEmpty());
}

TEST_F(HelperBusTest, ADisconnectedOwnerLosesItsPendingPlans) {
  {
    // The connection must be fully released for the bus to see it leave.
    QDBusConnection leaving = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("helper-leaving"));
    const Prepared prepared = prepare(leaving);
    ASSERT_FALSE(prepared.id.isEmpty());
    EXPECT_EQ(plans_->planCount(), 1U);
  }
  QDBusConnection::disconnectFromBus(QStringLiteral("helper-leaving"));
  for (int i = 0; i < 100 && plans_->planCount() != 0; ++i) {
    pump(20);
  }
  EXPECT_EQ(plans_->planCount(), 0U);
}

TEST_F(HelperBusTest, DecisionChoicesTravelAsStructuredData) {
  engine_.resolveResult = std::unexpected(TransactionFailure{
      .code = TransactionErrorCode::NeedsDecision,
      .names = {},
      .choices =
          {
              holonight_packages_domain::NeedsDecisionChoice{
                  .kind = holonight_packages_domain::NeedsDecisionChoice::Kind::Provider,
                  .subject = "editor",
                  .candidates = {"vi", "nano"},
              },
          },
  });
  const Prepared prepared = prepare(client_);
  EXPECT_EQ(prepared.status.code, QStringLiteral("NeedsDecision"));
  ASSERT_EQ(prepared.status.choices.size(), 1);
  EXPECT_EQ(prepared.status.choices[0].kind, QStringLiteral("provider"));
  EXPECT_EQ(prepared.status.choices[0].subject, QStringLiteral("editor"));
  EXPECT_EQ(prepared.status.choices[0].candidates, (QStringList{QStringLiteral("vi"), QStringLiteral("nano")}));
}

TEST_F(HelperBusTest, ProtocolVersionIsOne) {
  QDBusMessage get = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                    QStringLiteral("Get"));
  get << kInterface << QStringLiteral("ProtocolVersion");
  const QDBusMessage reply = await(client_.asyncCall(get));
  ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  EXPECT_EQ(reply.arguments().at(0).value<QDBusVariant>().variant().toUInt(), 1U);
}

}  // namespace
}  // namespace holonight_helper

#include "helper_dbus_test.moc"
