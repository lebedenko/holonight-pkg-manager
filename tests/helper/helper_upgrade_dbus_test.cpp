#include "DBusCallerIdentity.h"
#include "HelperIdentity.h"
#include "HelperService.h"
#include "HelperSignals.h"
#include "HelperTerminator.h"
#include "QtWorkRunner.h"
#include "StateDirScratchRoots.h"
#include "WireTypes.h"
#include "fake_authorizer.h"
#include "fake_plan_id_generator.h"
#include "fake_transaction_engine.h"
#include "fixture_root.h"
#include "gpg_keyring.h"
#include "gpgme_guard.h"
#include "holonight_packages_application/update_check_runtime.h"
#include "holonight_packages_transaction/alpm_transaction_engine.h"
#include "private_bus.h"
#include "recording_terminator.h"
#include "repo_builder.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mutex>

namespace holonight_helper {
namespace {

namespace fs = std::filesystem;
using holonight_packages_application::AuthorizationDecision;
using holonight_packages_application::PlanService;
using holonight_packages_application::SystemClock;
using holonight_packages_domain::CancelDecision;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionControl;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_domain::UpgradeDetail;
using holonight_packages_domain::UpgradePrepared;
using holonight_packages_domain::UpgradePrepareOutcome;
using holonight_packages_domain::UpgradeWorkspace;
using holonight_packages_testing::FakeAuthorizer;
using holonight_packages_testing::FakePlanIdGenerator;
using holonight_packages_testing::FakeTransactionEngine;
using holonight_packages_testing::PrivateBus;
using holonight_packages_testing::RecordingTerminator;
using holonight_transaction_testing::FixtureRoot;
using holonight_transaction_testing::GpgKeyring;
using holonight_transaction_testing::PackageSpec;
using holonight_transaction_testing::RepoBuilder;

const QString kService = QString::fromLatin1(kBusName);
const QString kPath = QString::fromLatin1(kObjectPath);
const QString kInterface = QString::fromLatin1(kInterfaceName);

QDBusMessage await(const QDBusPendingCall& pending, int milliseconds = 20000) {
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

// Pumps the event loop until `done` or the limit; true when done.
template <typename Predicate>
bool pumpUntil(Predicate done, std::chrono::milliseconds limit = std::chrono::seconds(5)) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (!done() && std::chrono::steady_clock::now() < end) {
    pump(10);
  }
  return done();
}

// Holds the work of a fake engine until release().
struct Gate {
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false;
  bool released = false;
  void enterAndWait() {
    std::unique_lock lock(mutex);
    entered = true;
    condition.notify_all();
    condition.wait(lock, [this] { return released; });
  }
  void release() {
    const std::scoped_lock lock(mutex);
    released = true;
    condition.notify_all();
  }
  bool hasEntered() {
    const std::scoped_lock lock(mutex);
    return entered;
  }
};

class ProgressRecorder : public QObject {
  Q_OBJECT

 public:
  QList<QStringList> received;

 public slots:
  void onProgress(const QString& planId, const QString& phase, uint current, uint total, const QString& item) {
    received.push_back({planId, phase, QString::number(current), QString::number(total), item});
  }
};

UpgradePrepared fakeUpgrade() {
  ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  plan.upgrade = UpgradeDetail{
      .upgrades = {{.name = "app", .old_version = "1", .new_version = "2", .repository = "core"}},
      .replacements = {},
      .new_dependencies = {},
      .held_back = {},
      .keyring_packages = {},
      .download_bytes = 10,
      .size_change_bytes = 5,
  };
  return UpgradePrepared{.plan = plan, .guard = {}};
}

// The helper service on a private bus. `engine()` picks the engine: a scriptable fake, or the real libalpm engine on a
// fixture world. Scratch roots are real directories under a temporary state directory.
class HelperUpgradeBusTest : public ::testing::Test {
 public:
  void SetUp() override {
    registerWireTypes();
    ASSERT_TRUE(state_.isValid());
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    static std::atomic<int> counter{0};
    const QString suffix = QString::number(counter++);
    server_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("upgrade-server-") + suffix);
    client_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("upgrade-client-") + suffix);
    other_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("upgrade-other-") + suffix);
    ASSERT_TRUE(server_.isConnected() && client_.isConnected() && other_.isConnected());
    fake_.prepareUpgradeResult.result = fakeUpgrade();
    scratch_ = std::make_unique<StateDirScratchRoots>(stateDir());
  }

  void startService(holonight_packages_domain::TransactionEngine& engine,
                    holonight_packages_application::Terminator* terminator = nullptr) {
    identities_ = std::make_unique<DBusCallerIdentity>(server_);
    plans_ = std::make_unique<PlanService>(PlanService::Dependencies{
        .engine = engine,
        .authorizer = authorizer_,
        .identities = *identities_,
        .ids = ids_,
        .clock = clock_,
        .runner = runner_,
        .terminator = terminator != nullptr ? *terminator : terminator_,
        .scratch = scratch_.get(),
    });
    service_ = std::make_unique<HelperService>(server_, *plans_);
    ASSERT_TRUE(service_->start());
  }

  void TearDown() override {
    service_.reset();
    plans_.reset();
    QDBusConnection::disconnectFromBus(server_.name());
    QDBusConnection::disconnectFromBus(client_.name());
    QDBusConnection::disconnectFromBus(other_.name());
  }

  [[nodiscard]] fs::path stateDir() const { return fs::path(state_.path().toStdString()) / "state"; }
  [[nodiscard]] int scratchRootCount() const {
    int count = 0;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(stateDir(), error)) {
      count += entry.path().filename().string().starts_with("upgrade-") ? 1 : 0;
    }
    return count;
  }

  static QDBusPendingCall start(QDBusConnection& from, const QString& method, const QVariantList& arguments,
                                int timeout = 30000) {
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    message.setArguments(arguments);
    return from.asyncCall(message, timeout);
  }

  struct Prepared {
    StatusWire status;
    QString id;
    UpgradePreviewWire preview;
    QByteArray raw;  // every byte of the reply, for the scratch path search
  };
  static Prepared decodePrepared(const QDBusMessage& reply) {
    Prepared prepared;
    if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() == 3) {
      prepared.status = qdbus_cast<StatusWire>(reply.arguments().at(0));
      prepared.id = reply.arguments().at(1).toString();
      prepared.preview = qdbus_cast<UpgradePreviewWire>(reply.arguments().at(2));
      prepared.raw = describe(prepared).toUtf8();
    } else {
      ADD_FAILURE() << reply.errorName().toStdString() << ": " << reply.errorMessage().toStdString();
    }
    return prepared;
  }
  static QString describe(const Prepared& prepared) {
    QStringList parts{prepared.status.code, prepared.id};
    parts += prepared.status.names;
    for (const auto& entry : prepared.preview.upgrades) {
      parts << entry.name << entry.old_version << entry.new_version << entry.repository;
    }
    for (const auto& entry : prepared.preview.replacements) {
      parts << entry.old_name << entry.new_name << entry.repository;
    }
    for (const auto& entry : prepared.preview.new_dependencies) {
      parts << entry.name << entry.version << entry.repository;
    }
    for (const auto& entry : prepared.preview.held_back) {
      parts << entry.name << entry.installed_version;
    }
    parts += prepared.preview.keyring_packages;
    return parts.join(QLatin1Char(' '));
  }
  static Prepared prepare(QDBusConnection& from, const QVariantMap& options = {}) {
    return decodePrepared(await(start(from, QStringLiteral("PrepareUpgrade"), {options}, 600000)));
  }
  static ResultWire execute(QDBusConnection& from, const QString& plan_id) {
    const QDBusMessage reply = await(start(from, QStringLiteral("Execute"), {plan_id}));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
      ADD_FAILURE() << reply.errorName().toStdString();
      return {};
    }
    return qdbus_cast<ResultWire>(reply.arguments().at(0));
  }
  static StatusWire cancel(QDBusConnection& from, const QString& plan_id) {
    const QDBusMessage reply = await(start(from, QStringLiteral("Cancel"), {plan_id}));
    return reply.arguments().isEmpty() ? StatusWire{.code = QStringLiteral("no reply"), .names = {}, .choices = {}}
                                       : qdbus_cast<StatusWire>(reply.arguments().at(0));
  }

  QTemporaryDir state_;
  PrivateBus bus_;
  QDBusConnection server_{QStringLiteral("unused")};
  QDBusConnection client_{QStringLiteral("unused")};
  QDBusConnection other_{QStringLiteral("unused")};
  FakeTransactionEngine fake_;
  FakeAuthorizer authorizer_;
  FakePlanIdGenerator ids_;
  SystemClock clock_;
  QtWorkRunner runner_;
  RecordingTerminator terminator_;
  std::unique_ptr<StateDirScratchRoots> scratch_;
  std::unique_ptr<DBusCallerIdentity> identities_;
  std::unique_ptr<PlanService> plans_;
  std::unique_ptr<HelperService> service_;
};

// ---- T-049: the method, its options and the protocol version ----

TEST_F(HelperUpgradeBusTest, AnOptionIsInvalidArgumentAndNeverReachesTheEngine) {
  startService(fake_);
  const Prepared prepared = prepare(client_, {{QStringLiteral("noconfirm"), true}});
  EXPECT_EQ(prepared.status.code, QStringLiteral("InvalidArgument"));
  EXPECT_TRUE(prepared.id.isEmpty());
  EXPECT_EQ(fake_.prepareUpgradeCalls(), 0);
  EXPECT_EQ(scratchRootCount(), 0);
}

TEST_F(HelperUpgradeBusTest, NothingToDoCarriesAnEmptyPlanIdAndLeavesNoScratchRoot) {
  fake_.prepareUpgradeResult.result =
      std::unexpected(TransactionFailure{.code = TransactionErrorCode::NothingToDo, .names = {}, .choices = {}});
  startService(fake_);
  const Prepared prepared = prepare(client_);
  EXPECT_EQ(prepared.status.code, QStringLiteral("NothingToDo"));
  EXPECT_TRUE(prepared.id.isEmpty());
  EXPECT_EQ(scratchRootCount(), 0);
}

TEST_F(HelperUpgradeBusTest, ThePreviewTravelsAndTheScratchRootStaysWithThePlan) {
  startService(fake_);
  const Prepared prepared = prepare(client_);
  ASSERT_TRUE(prepared.status.code.isEmpty()) << prepared.status.code.toStdString();
  EXPECT_EQ(prepared.id.size(), 32);
  ASSERT_EQ(prepared.preview.upgrades.size(), 1);
  EXPECT_EQ(prepared.preview.upgrades[0].name, QStringLiteral("app"));
  EXPECT_EQ(prepared.preview.download_bytes, 10U);
  EXPECT_GT(prepared.preview.expires_at, 0);
  EXPECT_EQ(scratchRootCount(), 1);
  EXPECT_FALSE(prepared.raw.contains(QByteArray(stateDir().string().c_str())));  // REQ-F-008
}

TEST_F(HelperUpgradeBusTest, TheXmlDeclaresTheFourMethodsAndTheTenMinuteTimeout) {
  QFile file(QString::fromLatin1(HOLONIGHT_HELPER_DBUS_XML));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const QByteArray xml = file.readAll();
  EXPECT_TRUE(xml.contains(R"(<method name="PrepareUpgrade">)"));
  EXPECT_TRUE(xml.contains(R"(<arg direction="in" name="options" type="a{sv}"/>)"));
  EXPECT_TRUE(xml.contains("10 minutes"));
  EXPECT_EQ(xml.count("<method name="), 4);
}

// ---- T-050: progress to the owner only, and answers while the work is held ----

TEST_F(HelperUpgradeBusTest, SyncingGoesToTheCallerOnlyWithAnEmptyPlanId) {
  fake_.prepareUpgradeScript = [this](const fs::path&, const ExecutionContext& context) {
    context.progress(ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 1, .total = 2, .item = "core"});
    context.progress(ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 2, .total = 2, .item = "extra"});
    return fake_.prepareUpgradeResult;
  };
  startService(fake_);
  ProgressRecorder owner;
  ProgressRecorder stranger;
  ASSERT_TRUE(client_.connect(kService, kPath, kInterface, QStringLiteral("Progress"), &owner,
                              SLOT(onProgress(QString, QString, uint, uint, QString))));
  ASSERT_TRUE(other_.connect(kService, kPath, kInterface, QStringLiteral("Progress"), &stranger,
                             SLOT(onProgress(QString, QString, uint, uint, QString))));
  ASSERT_TRUE(prepare(client_).status.code.isEmpty());
  pumpUntil([&owner] { return owner.received.size() >= 2; });
  ASSERT_EQ(owner.received.size(), 2);
  EXPECT_EQ(owner.received[0], (QStringList{QString(), QStringLiteral("Syncing"), QStringLiteral("1"),
                                            QStringLiteral("2"), QStringLiteral("core")}));
  EXPECT_EQ(owner.received[1][2], QStringLiteral("2"));
  EXPECT_TRUE(stranger.received.isEmpty());
}

TEST_F(HelperUpgradeBusTest, WhileAPreparationIsHeldPropertiesAndForeignCancelsAnswerWithinASecond) {
  Gate gate;
  fake_.prepareUpgradeScript = [this, &gate](const fs::path&, const ExecutionContext&) {
    gate.enterAndWait();
    return fake_.prepareUpgradeResult;
  };
  startService(fake_);
  const QDBusPendingCall pending = start(client_, QStringLiteral("PrepareUpgrade"), {QVariantMap()}, 600000);
  ASSERT_TRUE(pumpUntil([&gate] { return gate.hasEntered(); }));
  const auto started = std::chrono::steady_clock::now();
  QDBusMessage get = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                    QStringLiteral("Get"));
  get << kInterface << QStringLiteral("Busy");
  const QDBusMessage busy = await(other_.asyncCall(get));
  EXPECT_TRUE(busy.arguments().at(0).value<QDBusVariant>().variant().toBool());
  EXPECT_EQ(cancel(other_, QStringLiteral("0123456789abcdef0123456789abcdef")).code, QStringLiteral("PlanNotFound"));
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
  // A second preparation is Busy at once (REQ-F-004).
  EXPECT_EQ(prepare(other_).status.code, QStringLiteral("Busy"));
  gate.release();
  EXPECT_TRUE(decodePrepared(await(pending)).status.code.isEmpty());
}

TEST_F(HelperUpgradeBusTest, SigtermDuringHeldPreparationKeepsServicesAliveUntilFinalization) {
  Gate gate;
  fake_.prepareUpgradeScript = [this, &gate](const fs::path&, const ExecutionContext&) {
    gate.enterAndWait();
    return fake_.prepareUpgradeResult;
  };
  bool finalized = false;
  bool quitImmediately = false;
  HelperTerminator terminator(
      [&] {
        EXPECT_FALSE(quitImmediately);
        EXPECT_NE(service_, nullptr);
        EXPECT_TRUE(plans_->hasActivity());
        finalized = true;
      },
      std::chrono::milliseconds(200));
  startService(fake_, &terminator);
  HelperSignals signalBridge;
  QObject::connect(&signalBridge, &HelperSignals::terminationRequested, service_.get(),
                   [&] { quitImmediately = plans_->shutdownRequested(); });
  const auto pending = start(client_, QStringLiteral("PrepareUpgrade"), {QVariantMap()}, 600000);
  ASSERT_TRUE(pumpUntil([&gate] { return gate.hasEntered(); }));
  std::raise(SIGTERM);
  EXPECT_EQ(decodePrepared(await(pending)).status.code, QStringLiteral("Cancelled"));
  EXPECT_FALSE(quitImmediately);
  EXPECT_FALSE(finalized);
  EXPECT_EQ(prepare(other_).status.code, QStringLiteral("Busy"));
  gate.release();
  EXPECT_TRUE(pumpUntil([&] { return finalized; }));
  EXPECT_FALSE(plans_->shutdownRequested());
}

// ---- T-046: cancel and disconnect during preparation ----

TEST_F(HelperUpgradeBusTest, CancelWithAnEmptyIdEndsThePreparationAfterTheReply) {
  Gate gate;
  fake_.prepareUpgradeScript = [this, &gate](const fs::path&, const ExecutionContext&) {
    gate.enterAndWait();
    return fake_.prepareUpgradeResult;
  };
  startService(fake_);
  const QDBusPendingCall pending = start(client_, QStringLiteral("PrepareUpgrade"), {QVariantMap()}, 600000);
  ASSERT_TRUE(pumpUntil([&gate] { return gate.hasEntered(); }));
  EXPECT_EQ(scratchRootCount(), 1);
  EXPECT_EQ(cancel(other_, QString()).code, QStringLiteral("PlanNotFound"));  // REQ-F-033
  EXPECT_EQ(terminator_.calls(), 0);
  EXPECT_TRUE(cancel(client_, QString()).code.isEmpty());
  const Prepared prepared = decodePrepared(await(pending));
  EXPECT_EQ(prepared.status.code, QStringLiteral("Cancelled"));
  EXPECT_EQ(scratchRootCount(), 0);
  EXPECT_EQ(terminator_.calls(), 1);
  gate.release();
  pump(100);
}

TEST_F(HelperUpgradeBusTest, TheOwnerDisconnectingDuringPreparationRemovesTheScratchRoot) {
  Gate gate;
  fake_.prepareUpgradeScript = [this, &gate](const fs::path&, const ExecutionContext&) {
    gate.enterAndWait();
    return fake_.prepareUpgradeResult;
  };
  startService(fake_);
  {
    // The connection must be fully released for the bus to see it leave.
    QDBusConnection leaving = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("upgrade-leaving"));
    static_cast<void>(start(leaving, QStringLiteral("PrepareUpgrade"), {QVariantMap()}, 600000));
    ASSERT_TRUE(pumpUntil([&gate] { return gate.hasEntered(); }));
    ASSERT_EQ(scratchRootCount(), 1);
  }
  QDBusConnection::disconnectFromBus(QStringLiteral("upgrade-leaving"));
  EXPECT_TRUE(pumpUntil([this] { return scratchRootCount() == 0; }, std::chrono::seconds(1)));
  EXPECT_EQ(terminator_.calls(), 1);
  gate.release();
  pump(100);
}

// ---- T-045, T-057, T-058: execution over the bus ----

TEST_F(HelperUpgradeBusTest, ADenialKeepsThePlanAndTheScratchRootAndASecondExecuteRunsIt) {
  startService(fake_);
  const Prepared prepared = prepare(client_);
  authorizer_.answer = AuthorizationDecision::NotAuthorized;
  EXPECT_EQ(execute(client_, prepared.id).status.code, QStringLiteral("NotAuthorized"));
  EXPECT_EQ(fake_.executeUpgradeCalls(), 0);
  EXPECT_EQ(scratchRootCount(), 1);
  authorizer_.answer = AuthorizationDecision::Authorized;
  const ResultWire result = execute(client_, prepared.id);
  EXPECT_EQ(result.outcome, QStringLiteral("Succeeded"));
  EXPECT_EQ(scratchRootCount(), 0);  // removed before the reply (REQ-F-014)
  ASSERT_EQ(authorizer_.requests().size(), 2U);
  EXPECT_EQ(authorizer_.requests().back().action_id, "org.holonight.packages.upgrade");
}

TEST_F(HelperUpgradeBusTest, ACancelDuringTheFetchIsCancelledAndEndsTheProcessAfterTheReply) {
  Gate gate;
  struct AcceptingControl final : ExecutionControl {
    CancelDecision requestCancel() noexcept override { return CancelDecision::Accepted; }
  } control;
  fake_.executeUpgradeScript = [&](const ResolvedPlan&, const UpgradeWorkspace&, const ExecutionContext& context) {
    context.control_changed(&control);
    gate.enterAndWait();
    context.control_changed(nullptr);
    return ExecutionResult::succeeded();
  };
  startService(fake_);
  const Prepared prepared = prepare(client_);
  const QDBusPendingCall pending = start(client_, QStringLiteral("Execute"), {prepared.id});
  ASSERT_TRUE(pumpUntil([&gate] { return gate.hasEntered(); }));
  EXPECT_EQ(prepare(other_).status.code, QStringLiteral("Busy"));  // REQ-F-038
  EXPECT_TRUE(cancel(client_, prepared.id).code.isEmpty());
  const QDBusMessage reply = await(pending);
  EXPECT_EQ(qdbus_cast<ResultWire>(reply.arguments().at(0)).outcome, QStringLiteral("Cancelled"));
  EXPECT_EQ(terminator_.calls(), 1);
  EXPECT_EQ(scratchRootCount(), 0);
  gate.release();
  pump(100);
}

TEST_F(HelperUpgradeBusTest, ACancelAfterPromotionIsPastThePointOfNoReturnAndTheUpgradeGoesOn) {
  Gate gate;
  struct RefusingControl final : ExecutionControl {
    CancelDecision requestCancel() noexcept override { return CancelDecision::PastPointOfNoReturn; }
  } control;
  fake_.executeUpgradeScript = [&](const ResolvedPlan&, const UpgradeWorkspace&, const ExecutionContext& context) {
    context.control_changed(&control);
    gate.enterAndWait();
    context.control_changed(nullptr);
    return ExecutionResult::succeeded();
  };
  startService(fake_);
  const Prepared prepared = prepare(client_);
  const QDBusPendingCall pending = start(client_, QStringLiteral("Execute"), {prepared.id});
  ASSERT_TRUE(pumpUntil([&gate] { return gate.hasEntered(); }));
  EXPECT_EQ(cancel(client_, prepared.id).code, QStringLiteral("PastPointOfNoReturn"));
  gate.release();
  const QDBusMessage reply = await(pending);
  EXPECT_EQ(qdbus_cast<ResultWire>(reply.arguments().at(0)).outcome, QStringLiteral("Succeeded"));
  EXPECT_EQ(terminator_.calls(), 0);
}

// ---- T-054: every Execute outcome is journaled, also when the owner has gone ----

TEST_F(HelperUpgradeBusTest, TheOutcomeOfAnUpgradeWhoseOwnerLeftIsInTheJournal) {
  Gate gate;
  struct RefusingControl final : ExecutionControl {
    CancelDecision requestCancel() noexcept override { return CancelDecision::PastPointOfNoReturn; }
  } control;
  fake_.executeUpgradeScript = [&](const ResolvedPlan&, const UpgradeWorkspace&, const ExecutionContext& context) {
    context.control_changed(&control);
    gate.enterAndWait();
    context.control_changed(nullptr);
    return ExecutionResult::failed(TransactionErrorCode::UpgradePartiallyApplied);
  };
  startService(fake_);
  std::vector<std::string> journal;
  service_->setJournal(
      [&journal](holonight_packages_transaction::LogLevel, const std::string& line) { journal.push_back(line); });
  QString planId;
  {
    QDBusConnection leaving = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("upgrade-owner-leaving"));
    planId = prepare(leaving).id;
    static_cast<void>(start(leaving, QStringLiteral("Execute"), {planId}));
    ASSERT_TRUE(pumpUntil([&gate] { return gate.hasEntered(); }));
  }
  QDBusConnection::disconnectFromBus(QStringLiteral("upgrade-owner-leaving"));
  pump(200);
  gate.release();
  ASSERT_TRUE(pumpUntil([&journal] { return !journal.empty(); }));
  EXPECT_THAT(journal.back(), ::testing::HasSubstr(planId.toStdString()));
  EXPECT_THAT(journal.back(), ::testing::HasSubstr("Failed UpgradePartiallyApplied"));
  EXPECT_EQ(terminator_.calls(), 0);  // past the point of no return, a disconnect never ends the process
}

// ---- T-047, T-056, T-057: the real engine on a fixture world ----

class HelperUpgradeWorldTest : public HelperUpgradeBusTest {
 public:
  HelperUpgradeWorldTest() : keyring_(world_.base() / "signing"), core_(world_, "core") {
    keyring_.trustIn(world_.gpgDir());
    core_.setSigner(keyring_.signer());
  }

  static PackageSpec pkg(std::string name, std::string version = "1.0-1") {
    PackageSpec spec;
    spec.files = {{"usr/share/" + name + "/" + version, name}};
    spec.name = std::move(name);
    spec.version = std::move(version);
    return spec;
  }

  // Server ahead of the live databases, as in the transaction tests.
  void serverAhead() const {
    const fs::path saved = world_.base() / "saved";
    fs::create_directories(saved);
    for (const auto& entry : fs::directory_iterator(world_.syncDbPath())) {
      fs::copy_file(entry.path(), saved / entry.path().filename());
    }
    core_.publish();
    for (const auto& entry : fs::directory_iterator(saved)) {
      const fs::path target = world_.syncDbPath() / entry.path().filename();
      fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
      fs::last_write_time(target, fs::file_time_type::clock::now() - std::chrono::hours(24));
    }
  }

  void startWorld(std::vector<std::string> extraOptions = {}) {
    holonight_transaction_testing::ConfSpec spec;
    spec.globalSigLevel = "Required DatabaseOptional";
    spec.extraOptions = std::move(extraOptions);
    spec.repos = {{.name = "core", .servers = {world_.serverUrl("core")}, .sigLevel = std::nullopt}};
    world_.writeConf(spec);
    holonight_packages_transaction::TransactionConfig config = world_.config();
    config.upgrade_limits = limits_;
    holonight_packages_transaction::TransactionSeams seams;
    seams.fetch = fetch_;
    engine_ = std::make_unique<holonight_packages_transaction::AlpmTransactionEngine>(
        config, [](holonight_packages_transaction::LogLevel, const std::string&) {}, std::move(seams));
    startService(*engine_);
  }

  // Declared first, so it outlives everything a stuck worker might still use.
  std::unique_ptr<holonight_packages_transaction::AlpmTransactionEngine> engine_;
  FixtureRoot world_;
  GpgKeyring keyring_;
  RepoBuilder core_;
  holonight_packages_transaction::UpgradeLimits limits_;
  std::function<int(const std::string&, const std::string&, bool)> fetch_;
};

TEST_F(HelperUpgradeWorldTest, AnUpgradeWithAReplacementAndAHeldPackageRunsEndToEnd) {
  SKIP_UNLESS_FRESH_GPGME();
  for (const std::string name : {"app", "foo", "oldpkg"}) {
    core_.add(pkg(name));
    world_.installPackage(pkg(name));
  }
  core_.publish();
  core_.add(pkg("app", "2.0-1"));
  core_.add(pkg("foo", "2.0-1"));
  PackageSpec replacing = pkg("newpkg");
  replacing.replaces = {"oldpkg"};
  core_.add(replacing);
  serverAhead();
  startWorld({"HoldPkg = foo"});
  const Prepared prepared = prepare(client_);
  ASSERT_TRUE(prepared.status.code.isEmpty()) << prepared.status.code.toStdString();
  EXPECT_FALSE(prepared.raw.contains(QByteArray(stateDir().string().c_str())));
  const ResultWire result = execute(client_, prepared.id);
  ASSERT_EQ(result.outcome, QStringLiteral("Succeeded")) << result.status.code.toStdString();
  EXPECT_FALSE(world_.isInstalled("oldpkg"));
  EXPECT_TRUE(world_.isInstalled("newpkg"));
  EXPECT_EQ(world_.installedVersions().at("foo"), "2.0-1");  // HoldPkg does not block an upgrade
  QStringList installed;
  for (const ChangeWire& change : result.changed) {
    if (change.action == QStringLiteral("install")) {
      installed << change.name;
    }
  }
  installed.sort();
  EXPECT_EQ(installed, (QStringList{QStringLiteral("app"), QStringLiteral("foo"), QStringLiteral("newpkg")}));
  EXPECT_TRUE(fs::exists(world_.cacheDir() / pkg("app", "2.0-1").fileName()));
  EXPECT_EQ(scratchRootCount(), 0);
}

TEST_F(HelperUpgradeWorldTest, AnExternalSyncBeforeExecuteIsSyncDatabaseChangedWithTheScratchRootGone) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  world_.installPackage(pkg("app"));
  core_.publish();
  core_.add(pkg("app", "2.0-1"));
  serverAhead();
  startWorld();
  const Prepared prepared = prepare(client_);
  ASSERT_TRUE(prepared.status.code.isEmpty()) << prepared.status.code.toStdString();
  core_.publish();  // the live databases catch up
  const ResultWire result = execute(client_, prepared.id);
  EXPECT_EQ(result.status.code, QStringLiteral("SyncDatabaseChanged"));
  EXPECT_EQ(scratchRootCount(), 0);
  EXPECT_EQ(world_.installedVersions().at("app"), "1.0-1");
}

TEST_F(HelperUpgradeWorldTest, TheInternalTimeoutIsSyncFailedAndTheProcessEndsAfterTheReply) {
  core_.add(pkg("app"));
  world_.installPackage(pkg("app"));
  core_.publish();
  core_.add(pkg("app", "2.0-1"));
  serverAhead();
  limits_.sync_timeout = std::chrono::seconds(1);
  auto released = std::make_shared<std::atomic<bool>>(false);
  fetch_ = [released](const std::string&, const std::string&, bool) {
    while (!released->load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return -1;
  };
  startWorld();
  const auto started = std::chrono::steady_clock::now();
  const Prepared prepared = prepare(client_);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(6));
  EXPECT_EQ(prepared.status.code, QStringLiteral("SyncFailed"));
  EXPECT_EQ(terminator_.calls(), 1);
  EXPECT_EQ(scratchRootCount(), 0);
  released->store(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

}  // namespace
}  // namespace holonight_helper

#include "helper_upgrade_dbus_test.moc"
