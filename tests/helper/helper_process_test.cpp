#include "HelperIdentity.h"
#include "WireTypes.h"
#include "fixture_root.h"
#include "private_bus.h"
#include "repo_builder.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/types.h>

namespace holonight_helper {
namespace {

using holonight_packages_testing::PrivateBus;
using holonight_transaction_testing::FixtureRoot;
using holonight_transaction_testing::PackageSpec;
using holonight_transaction_testing::RepoBuilder;

const QString kService = QString::fromLatin1(kBusName);
const QString kPath = QString::fromLatin1(kObjectPath);
const QString kInterface = QString::fromLatin1(kInterfaceName);

void pump(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

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

// A private bus that starts the real helper on demand, exactly as the system bus would, with the helper pointed at a
// fixture world instead of the real system (REQ-F-067, REQ-F-069).
class HelperProcessTest : public ::testing::Test {
 public:
  void SetUp() override {
    registerWireTypes();
    ASSERT_TRUE(directory_.isValid());
    const QString root = directory_.path();
    const QString socket = root + QStringLiteral("/bus.sock");
    const QString services = root + QStringLiteral("/services");
    ASSERT_TRUE(QDir().mkpath(services));
    address_ = QStringLiteral("unix:path=") + socket;

    RepoBuilder repo(world_, "core");
    repo.add(PackageSpec{.name = "app", .files = {{"usr/share/app/file", "app"}}});
    repo.publish();
    world_.writeConf({"core"}, "Required DatabaseOptional");

    QFile activation(services + QStringLiteral("/org.holonight.PackageHelper1.service"));
    ASSERT_TRUE(activation.open(QIODevice::WriteOnly));
    QTextStream(&activation) << "[D-BUS Service]\nName=" << kService << "\nExec=" HOLONIGHT_PACKAGE_HELPER_EXECUTABLE
                             << " --root " << QString::fromStdString(world_.installRoot().string()) << " --dbpath "
                             << QString::fromStdString(world_.dbPath().string()) << " --pacman-conf "
                             << QString::fromStdString(world_.confPath().string()) << " --cache-dir "
                             << QString::fromStdString(world_.cacheDir().string()) << " --state-dir "
                             << QString::fromStdString((world_.base() / "state").string()) << "\n";
    activation.close();

    QFile configuration(root + QStringLiteral("/bus.conf"));
    ASSERT_TRUE(configuration.open(QIODevice::WriteOnly));
    QTextStream(&configuration)
        << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" "
           "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n<busconfig>\n"
           "<type>session</type>\n<listen>"
        << address_ << "</listen>\n<servicedir>" << services
        << "</servicedir>\n<policy context=\"default\"><allow send_destination=\"*\" "
           "eavesdrop=\"true\"/><allow eavesdrop=\"true\"/><allow own=\"*\"/></policy>\n</busconfig>\n";
    configuration.close();

    // The activated helper opens QDBusConnection::systemBus(); point that at the private bus.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"), address_);
    bus_.setProcessEnvironment(environment);
    ASSERT_TRUE(bus_.start(root + QStringLiteral("/bus.conf"))) << bus_.error().toStdString();
    static int counter = 0;
    connectionName_ = QStringLiteral("helper-process-client-%1").arg(counter++);
    client_ = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(address_, connectionName_));
    ASSERT_TRUE(client_->isConnected());
  }

  void TearDown() override {
    // Stop an activated helper so no process outlives the test.
    if (const pid_t pid = helperPid(); pid > 0) {
      ::kill(pid, SIGKILL);
    }
    client_.reset();
    QDBusConnection::disconnectFromBus(connectionName_);
  }

  [[nodiscard]] pid_t helperPid() const {
    if (!client_ || !client_->interface()->isServiceRegistered(kService)) {
      return -1;
    }
    const auto reply = client_->interface()->servicePid(kService);
    return reply.isValid() ? static_cast<pid_t>(reply.value()) : -1;
  }

  [[nodiscard]] QDBusMessage call(const QString& method, const QVariantList& arguments) const {
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    message.setArguments(arguments);
    return await(client_->asyncCall(message, 30000));
  }

  PrivateBus bus_;
  QTemporaryDir directory_;
  FixtureRoot world_;
  QString address_;
  QString connectionName_;
  std::unique_ptr<QDBusConnection> client_;
};

TEST_F(HelperProcessTest, TheFirstCallStartsTheHelperAndIsAnsweredWithinTwoSeconds) {
  ASSERT_FALSE(client_->interface()->isServiceRegistered(kService));
  const auto started = std::chrono::steady_clock::now();
  const QDBusMessage reply = call(QStringLiteral("PreparePlan"),
                                  {QStringLiteral("install"), QStringList{QStringLiteral("app")}, QVariantMap()});
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
  EXPECT_LT(elapsed, std::chrono::seconds(2));
  const auto status = qdbus_cast<StatusWire>(reply.arguments().at(0));
  EXPECT_TRUE(status.code.isEmpty()) << status.code.toStdString();
  EXPECT_EQ(reply.arguments().at(1).toString().size(), 32);
  EXPECT_TRUE(client_->interface()->isServiceRegistered(kService));
  EXPECT_GT(helperPid(), 0);
}

TEST_F(HelperProcessTest, WithoutPolkitAnExecuteIsNotAuthorizedAndInstallsNothing) {
  const QDBusMessage prepared = call(QStringLiteral("PreparePlan"),
                                     {QStringLiteral("install"), QStringList{QStringLiteral("app")}, QVariantMap()});
  ASSERT_EQ(prepared.type(), QDBusMessage::ReplyMessage);
  const QString plan_id = prepared.arguments().at(1).toString();
  const QDBusMessage executed = call(QStringLiteral("Execute"), {plan_id});
  ASSERT_EQ(executed.type(), QDBusMessage::ReplyMessage) << executed.errorMessage().toStdString();
  const auto result = qdbus_cast<ResultWire>(executed.arguments().at(0));
  EXPECT_EQ(result.outcome, QStringLiteral("Failed"));
  EXPECT_EQ(result.status.code, QStringLiteral("NotAuthorized"));
  EXPECT_TRUE(world_.installedVersions().empty());
  EXPECT_FALSE(world_.fileExists("usr/share/app/file"));
}

TEST_F(HelperProcessTest, APlanFromBeforeARestartIsGone) {
  const QDBusMessage prepared = call(QStringLiteral("PreparePlan"),
                                     {QStringLiteral("install"), QStringList{QStringLiteral("app")}, QVariantMap()});
  ASSERT_EQ(prepared.type(), QDBusMessage::ReplyMessage);
  const QString plan_id = prepared.arguments().at(1).toString();
  const pid_t first = helperPid();
  ASSERT_GT(first, 0);
  ::kill(first, SIGKILL);
  for (int i = 0; i < 100 && client_->interface()->isServiceRegistered(kService); ++i) {
    pump(20);
  }
  ASSERT_FALSE(client_->interface()->isServiceRegistered(kService));

  // The next call starts a new helper process, which never heard of the plan (REQ-F-069).
  const QDBusMessage executed = call(QStringLiteral("Execute"), {plan_id});
  ASSERT_EQ(executed.type(), QDBusMessage::ReplyMessage) << executed.errorMessage().toStdString();
  EXPECT_EQ(qdbus_cast<ResultWire>(executed.arguments().at(0)).status.code, QStringLiteral("PlanNotFound"));
  const pid_t second = helperPid();
  EXPECT_GT(second, 0);
  EXPECT_NE(second, first);
}

// system-upgrade T-053, REQ-F-015, REQ-F-016, REQ-F-077: what a killed helper left behind is gone before the first
// call is answered, and the live databases are untouched.
TEST_F(HelperProcessTest, LeftoversOfAKilledUpgradeAreSweptAtStart) {
  const std::filesystem::path stale = world_.base() / "state" / "upgrade-stale";
  std::filesystem::create_directories(stale / "dbpath" / "sync");
  ASSERT_TRUE(
      QFile(QString::fromStdString((stale / "dbpath" / "sync" / "core.db").string())).open(QIODevice::WriteOnly));
  std::filesystem::create_directories(world_.base() / "state" / "keep");
  const std::filesystem::path temporary = world_.syncDbPath() / ".hn-promote-core.db.tmp";
  ASSERT_TRUE(QFile(QString::fromStdString(temporary.string())).open(QIODevice::WriteOnly));
  const auto coreBefore = std::filesystem::file_size(world_.syncDbPath() / "core.db");
  const QDBusMessage reply = call(QStringLiteral("PreparePlan"),
                                  {QStringLiteral("install"), QStringList{QStringLiteral("app")}, QVariantMap()});
  ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
  EXPECT_FALSE(std::filesystem::exists(stale));
  EXPECT_FALSE(std::filesystem::exists(temporary));
  EXPECT_TRUE(std::filesystem::exists(world_.base() / "state" / "keep"));
  EXPECT_EQ(std::filesystem::file_size(world_.syncDbPath() / "core.db"), coreBefore);
}

}  // namespace
}  // namespace holonight_helper
