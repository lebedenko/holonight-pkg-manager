#include "UpdateStatusService.h"
#include "fake_clock.h"
#include "fake_snapshot_store.h"
#include "fake_update_checker.h"
#include "fake_update_source.h"
#include "file_repo_fixture.h"
#include "holonight_packages_application/update_check_service.h"
#include "holonight_packages_application/update_monitor.h"
#include "holonight_packages_backends/alpm_update_checker.h"
#include "holonight_packages_backends/backend_capabilities_alpm.h"
#include "private_bus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>

namespace {

namespace fs = std::filesystem;
using holonight_packages_application::CheckOrigin;
using holonight_packages_application::UpdateCheckPolicy;
using holonight_packages_application::UpdateCheckService;
using holonight_packages_application::UpdateMonitor;
using holonight_packages_application::UpdateMonitorOptions;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakeSnapshotStore;
using holonight_packages_testing::FakeUpdateChecker;
using holonight_packages_testing::FakeUpdateSource;
using holonight_packages_testing::PrivateBus;

constexpr int kWaitMs = 8000;
const QString kService = QString::fromLatin1(UpdateStatusService::kServiceName);
const QString kPath = QString::fromLatin1(UpdateStatusService::kObjectPath);
const QString kCheck = QString::fromLatin1(UpdateStatusService::kCheckInterfaceName);
const QString kUpdates = QString::fromLatin1(UpdateStatusService::kInterfaceName);

UpdateSnapshot snapshot(int count, long long dataAsOf = 1'700'000'000) {
  UpdateSnapshot result;
  for (int index = 0; index < count; ++index) {
    result.updates.push_back(PendingUpdate{.name = "n" + std::to_string(index), .downloadSizeBytes = 100});
  }
  result.dataAsOf = std::chrono::system_clock::time_point{std::chrono::seconds{dataAsOf}};
  return result;
}

QSet<QString> membersOf(const QByteArray& xml, const QString& interfaceName) {
  QSet<QString> members;
  QXmlStreamReader reader(xml);
  bool inInterface = false;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      const QString name = reader.name().toString();
      const auto attrs = reader.attributes();
      if (name == QLatin1String("interface")) {
        inInterface = attrs.value(QLatin1String("name")) == interfaceName;
      } else if (inInterface && name == QLatin1String("property")) {
        members.insert(QStringLiteral("property %1 %2 %3")
                           .arg(attrs.value(QLatin1String("name")), attrs.value(QLatin1String("type")),
                                attrs.value(QLatin1String("access"))));
      } else if (inInterface && (name == QLatin1String("method") || name == QLatin1String("signal"))) {
        members.insert(name + QLatin1Char(' ') + attrs.value(QLatin1String("name")).toString());
      }
    } else if (reader.isEndElement() && reader.name() == QLatin1String("interface")) {
      inInterface = false;
    }
  }
  return members;
}

// Counts StatusChanged signals and records UpdateCheck PropertiesChanged payloads.
class CheckListener : public QObject {
  Q_OBJECT

 public:
  int statusChanged = 0;
  QList<QVariantMap> changes;

 public slots:
  void onStatusChanged() { ++statusChanged; }
  void onPropertiesChanged(const QString& interface_name, const QVariantMap& changed, const QStringList& /*unused*/) {
    if (interface_name == QString::fromLatin1(UpdateStatusService::kCheckInterfaceName)) {
      changes.push_back(changed);
    }
  }
};

QDBusMessage callOn(QDBusConnection& client, const QString& interfaceName, const QString& method,
                    const QVariantList& args = {}, int* elapsedMs = nullptr) {
  QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, interfaceName, method);
  message.setArguments(args);
  QElapsedTimer timer;
  timer.start();
  QDBusPendingCall pending = client.asyncCall(message, kWaitMs);
  QDBusPendingCallWatcher watcher(pending);
  while (!watcher.isFinished() && timer.elapsed() < kWaitMs) {
    QTest::qWait(1);
  }
  if (elapsedMs != nullptr) {
    *elapsedMs = static_cast<int>(timer.elapsed());
  }
  EXPECT_TRUE(watcher.isFinished());
  return pending.reply();
}

QVariant propertyOn(QDBusConnection& client, const QString& name, int* elapsedMs = nullptr) {
  const QDBusMessage reply = callOn(client, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
                                    {kCheck, name}, elapsedMs);
  EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
  return reply.arguments().isEmpty() ? QVariant{} : reply.arguments().first().value<QDBusVariant>().variant();
}

class PackagedUpdateCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    serviceConnection_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("check-service"));
    client_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("check-client"));
    ASSERT_TRUE(serviceConnection_.isConnected());
    ASSERT_TRUE(client_.isConnected());
  }

  void TearDown() override {
    service_.reset();
    check_.reset();
    monitor_.reset();
    QDBusConnection::disconnectFromBus(QStringLiteral("check-service"));
    QDBusConnection::disconnectFromBus(QStringLiteral("check-client"));
  }

  void build(bool capable = true) {
    source_->enqueue(snapshot(7));
    monitor_ =
        std::make_unique<UpdateMonitor>(source_, UpdateMonitorOptions{.debounce = std::chrono::milliseconds{10}});
    check_ = std::make_unique<UpdateCheckService>(
        UpdateCheckService::Dependencies{
            .checker = checker_,
            .store = store_,
            .clock = clock_,
            .capabilities = {.canCheckForUpdates = capable},
        },
        UpdateCheckPolicy{});
    QObject::connect(check_.get(), &UpdateCheckService::snapshotAdopted, monitor_.get(), &UpdateMonitor::adoptOnline);
    service_ = std::make_unique<UpdateStatusService>(monitor_.get(), check_.get());
    ASSERT_TRUE(service_->registerOn(serviceConnection_)) << service_->error().toStdString();
    monitor_->start();
    ASSERT_TRUE(QTest::qWaitFor([&] { return monitor_->status().updateCount == 7; }, kWaitMs));
  }

  PrivateBus bus_;
  QDBusConnection serviceConnection_{QStringLiteral("")};
  QDBusConnection client_{QStringLiteral("")};
  std::shared_ptr<FakeUpdateSource> source_ = std::make_shared<FakeUpdateSource>();
  std::shared_ptr<FakeUpdateChecker> checker_ = std::make_shared<FakeUpdateChecker>();
  std::shared_ptr<FakeSnapshotStore> store_ = std::make_shared<FakeSnapshotStore>();
  std::shared_ptr<FakeClock> clock_ = std::make_shared<FakeClock>();
  std::unique_ptr<UpdateMonitor> monitor_;
  std::unique_ptr<UpdateCheckService> check_;
  std::unique_ptr<UpdateStatusService> service_;
};

TEST_F(PackagedUpdateCheckTest, OneCheckNowReachesTheCoordinatorOnceAndSetsChecking) {
  build();
  checker_->setBlocking(true);
  checker_->enqueue(snapshot(2, 1'800'000'000));

  const QDBusMessage reply = callOn(client_, kCheck, QStringLiteral("CheckNow"));

  EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  EXPECT_TRUE(reply.arguments().isEmpty());
  ASSERT_TRUE(QTest::qWaitFor([&] { return checker_->callCount() == 1; }, kWaitMs));
  EXPECT_EQ(propertyOn(client_, QStringLiteral("Checking")).toBool(), true);
  checker_->release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !check_->status().checking; }, kWaitMs));
  EXPECT_EQ(checker_->callCount(), 1);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("Checking")).toBool(), false);
}

TEST_F(PackagedUpdateCheckTest, PropertyReadAndCheckNowAnswerWithin100MsWhileACheckIsBlocked) {
  build();
  checker_->setBlocking(true);
  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return checker_->callCount() == 1; }, kWaitMs));

  int readMs = 0;
  int callMs = 0;
  (void)propertyOn(client_, QStringLiteral("LastCheckTime"), &readMs);
  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"), {}, &callMs);

  EXPECT_LT(readMs, 100);
  EXPECT_LT(callMs, 100);
  EXPECT_EQ(checker_->callCount(), 1) << "CheckNow while Checking joins the running check";
  checker_->release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !check_->status().checking; }, kWaitMs));
}

TEST_F(PackagedUpdateCheckTest, BackendWithoutTheCapabilityReportsCanCheckFalse) {
  build(false);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("CanCheck")).toBool(), false);
  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
  QTest::qWait(50);
  EXPECT_EQ(checker_->callCount(), 0);
}

TEST_F(PackagedUpdateCheckTest, NoSnapshotMeansZeroAndTheUpdatesCountIsUntouched) {
  build();
  EXPECT_EQ(propertyOn(client_, QStringLiteral("SnapshotFetchedAt")).toLongLong(), 0);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("LastCheckTime")).toLongLong(), 0);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("LastCheckSucceeded")).toBool(), false);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("LastCheckError")).toString(), "");
  const QDBusMessage count = callOn(client_, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
                                    {kUpdates, QStringLiteral("Count")});
  EXPECT_EQ(count.arguments().first().value<QDBusVariant>().variant().toUInt(), 7U);
}

TEST_F(PackagedUpdateCheckTest, CheckNowInsideTheCooldownMakesNoCall) {
  build();
  checker_->enqueue(snapshot(2, 1'800'000'000));
  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return checker_->callCount() == 1 && !check_->status().checking; }, kWaitMs));

  clock_->advance(std::chrono::seconds{3});
  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
  QTest::qWait(50);
  EXPECT_EQ(checker_->callCount(), 1);

  clock_->advance(std::chrono::seconds{8});
  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return checker_->callCount() == 2; }, kWaitMs));
}

TEST_F(PackagedUpdateCheckTest, CompletionEmitsStatusChangedOnceAfterPropertiesAndAdoptsTheSnapshot) {
  build();
  CheckListener listener;
  ASSERT_TRUE(
      client_.connect(kService, kPath, kCheck, QStringLiteral("StatusChanged"), &listener, SLOT(onStatusChanged())));
  ASSERT_TRUE(client_.connect(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                              QStringLiteral("PropertiesChanged"), &listener,
                              SLOT(onPropertiesChanged(QString, QVariantMap, QStringList))));
  checker_->enqueue(snapshot(2, 1'800'000'000));

  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return listener.statusChanged >= 1; }, kWaitMs));
  QTest::qWait(100);

  EXPECT_EQ(listener.statusChanged, 1);
  ASSERT_FALSE(listener.changes.isEmpty());
  EXPECT_EQ(listener.changes.last().value(QStringLiteral("Checking")).toBool(), false);
  EXPECT_NE(listener.changes.last().value(QStringLiteral("SnapshotFetchedAt")).toLongLong(), 0);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("LastCheckSucceeded")).toBool(), true);
  EXPECT_NE(propertyOn(client_, QStringLiteral("SnapshotFetchedAt")).toLongLong(), 0);
  // The fresher online snapshot is adopted into the monitor, so the sidebar count follows it.
  EXPECT_EQ(monitor_->status().updateCount, 2);
}

TEST_F(PackagedUpdateCheckTest, FailureIsReportedWithItsTokenAndKeepsTheCount) {
  build();
  checker_->enqueue(std::unexpected(holonight_packages_domain::UpdateCheckError{
      holonight_packages_domain::UpdateCheckErrorCode::NetworkUnavailable,
  }));
  (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return check_->status().hasCheckResult; }, kWaitMs));

  EXPECT_EQ(propertyOn(client_, QStringLiteral("LastCheckError")).toString(), "network-unavailable");
  EXPECT_EQ(propertyOn(client_, QStringLiteral("LastCheckSucceeded")).toBool(), false);
  EXPECT_NE(propertyOn(client_, QStringLiteral("LastCheckTime")).toLongLong(), 0);
  EXPECT_EQ(monitor_->status().updateCount, 7);
}

TEST_F(PackagedUpdateCheckTest, IntrospectionOfTheCheckInterfaceMatchesTheCheckedInContractAndTypes) {
  build();
  QFile contract(QString::fromLatin1(HOLONIGHT_PACKAGED_DBUS_XML));
  ASSERT_TRUE(contract.open(QIODevice::ReadOnly));
  const QSet<QString> expected = membersOf(contract.readAll(), kCheck);
  const QDBusMessage reply =
      callOn(client_, QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
  ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  const QSet<QString> actual = membersOf(reply.arguments().first().toString().toUtf8(), kCheck);

  EXPECT_EQ(actual, expected);
  EXPECT_TRUE(actual.contains(QStringLiteral("method CheckNow")));
  EXPECT_TRUE(actual.contains(QStringLiteral("signal StatusChanged")));
  EXPECT_EQ(actual.size(), 8);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("LastCheckTime")).metaType().id(), QMetaType::LongLong);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("SnapshotFetchedAt")).metaType().id(), QMetaType::LongLong);
  EXPECT_EQ(propertyOn(client_, QStringLiteral("CanCheck")).metaType().id(), QMetaType::Bool);
}

// ---- real adapter in-process: three D-Bus checks, one failing, one run directory ----------------------------------

TEST_F(PackagedUpdateCheckTest, ThreeRealChecksThroughTheBusLeaveExactlyOneRunDirectory) {
  holonight_packages_testing::FileRepoFixture fixture;
  auto real = std::make_shared<holonight_packages_backends::AlpmUpdateChecker>(
      holonight_packages_backends::AlpmUpdateCheckerOptions{
          .databaseRoot = fixture.realDbPath(),
          .databasePath = fixture.realDbPath(),
          .pacmanConfPath = fixture.confPath(),
          .scratchRoot = fixture.scratchRoot(),
          .totalBound = std::chrono::seconds{30},
          .hooks = {},
      });
  source_->enqueue(snapshot(1));
  monitor_ = std::make_unique<UpdateMonitor>(source_, UpdateMonitorOptions{});
  check_ = std::make_unique<UpdateCheckService>(
      UpdateCheckService::Dependencies{
          .checker = real,
          .store = store_,
          .clock = clock_,
          .capabilities = holonight_packages_backends::alpmBackendCapabilities(),
      },
      UpdateCheckPolicy{});
  service_ = std::make_unique<UpdateStatusService>(monitor_.get(), check_.get());
  ASSERT_TRUE(service_->registerOn(serviceConnection_));

  const auto runOne = [&] {
    clock_->advance(std::chrono::seconds{11});
    (void)callOn(client_, kCheck, QStringLiteral("CheckNow"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return check_->status().checking; }, 2000) || check_->status().hasCheckResult);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !check_->status().checking; }, kWaitMs));
  };
  runOne();
  EXPECT_TRUE(check_->status().lastCheckSucceeded);
  fixture.writeConf({"file://" + (fixture.root() / "missing").string()});
  runOne();
  EXPECT_FALSE(check_->status().lastCheckSucceeded);
  fixture.writeConf({"file://" + fixture.mirrorDir().string()});
  runOne();
  EXPECT_TRUE(check_->status().lastCheckSucceeded);

  int runs = 0;
  for (const auto& entry : fs::directory_iterator(fixture.scratchRoot())) {
    runs += entry.path().filename().string().starts_with("run-") ? 1 : 0;
  }
  EXPECT_EQ(runs, 1);
}

// ---- bus activation ------------------------------------------------------------------------------------------------

TEST(PackagedUpdateCheckActivationTest, CheckNowStartsPackagedFromTheActivationFile) {
  QTemporaryDir home;
  ASSERT_TRUE(home.isValid());
  holonight_packages_testing::FileRepoFixture fixture;
  const QString dataHome = home.filePath(QStringLiteral("data"));
  const QString services = dataHome + QStringLiteral("/dbus-1/services");
  ASSERT_TRUE(QDir().mkpath(services));
  {
    QFile file(services + QStringLiteral("/org.holonight.Packages1.service"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QString exec = QStringLiteral("%1 --root %2 --dbpath %2 --pacman-conf %3")
                             .arg(QString::fromLatin1(HOLONIGHT_PACKAGED_EXECUTABLE),
                                  QString::fromStdString(fixture.realDbPath().string()),
                                  QString::fromStdString(fixture.confPath().string()));
    file.write(("[D-BUS Service]\nName=org.holonight.Packages1\nExec=" + exec + "\n").toUtf8());
  }
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("HOME"), home.path());
  environment.insert(QStringLiteral("XDG_DATA_HOME"), dataHome);
  environment.insert(QStringLiteral("XDG_CONFIG_HOME"), home.filePath(QStringLiteral("config")));
  environment.insert(QStringLiteral("XDG_CACHE_HOME"), QString::fromStdString(fixture.cacheHome().string()));
  environment.remove(QStringLiteral("XDG_DATA_DIRS"));
  PrivateBus bus;
  bus.setProcessEnvironment(environment);
  ASSERT_TRUE(bus.start()) << bus.error().toStdString();
  QDBusConnection client = QDBusConnection::connectToBus(bus.address(), QStringLiteral("activation-client"));
  ASSERT_TRUE(client.isConnected());
  ASSERT_FALSE(client.interface()->isServiceRegistered(kService));

  // CheckNow on a name nobody owns starts holonight-packaged through the activation file.
  QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kCheck, QStringLiteral("CheckNow"));
  const QDBusMessage reply = client.call(message, QDBus::Block, 20000);
  EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
  EXPECT_TRUE(client.interface()->isServiceRegistered(kService));

  // The real checker ran against the fixture mirror and wrote the snapshot file under the isolated cache.
  const fs::path snapshotFile = fixture.cacheHome() / "holonight-packages" / "update-snapshot.json";
  EXPECT_TRUE(QTest::qWaitFor([&] { return fs::exists(snapshotFile); }, 20000));
  QDBusMessage get = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                    QStringLiteral("Get"));
  get.setArguments({kCheck, QStringLiteral("SnapshotFetchedAt")});
  EXPECT_TRUE(QTest::qWaitFor(
      [&] {
        const QDBusMessage value = client.call(get, QDBus::Block, 5000);
        return !value.arguments().isEmpty() &&
               value.arguments().first().value<QDBusVariant>().variant().toLongLong() != 0;
      },
      20000));
  QDBusConnection::disconnectFromBus(QStringLiteral("activation-client"));
}

}  // namespace

#include "packaged_update_check_dbus_test.moc"
