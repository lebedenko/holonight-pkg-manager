#include "DBusOwnerTracker.h"
#include "DBusUpdateCheckClient.h"
#include "UpdateStatusClient.h"
#include "UpdateStatusService.h"
#include "fake_update_source.h"
#include "holonight_packages_application/update_monitor.h"
#include "private_bus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <gtest/gtest.h>
#include <memory>

namespace {

using holonight_packages_application::UpdateMonitor;
using holonight_packages_application::UpdateMonitorOptions;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_testing::FakeUpdateSource;
using holonight_packages_testing::PrivateBus;

constexpr int kWaitMs = 5000;

UpdateSnapshot snapshotWithCount(int count) {
  UpdateSnapshot snapshot;
  for (int index = 0; index < count; ++index) {
    snapshot.updates.push_back(PendingUpdate{.name = "pkg-" + std::to_string(index)});
  }
  return snapshot;
}

class UpdateStatusClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    client_connection_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("client-side"));
    ASSERT_TRUE(client_connection_.isConnected());
  }

  void TearDown() override {
    client_.reset();
    service_.reset();
    monitor_.reset();
    QDBusConnection::disconnectFromBus(QStringLiteral("service-side"));
    QDBusConnection::disconnectFromBus(QStringLiteral("client-side"));
  }

  void startService() {
    service_connection_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("service-side"));
    source_->enqueue(snapshotWithCount(5));
    monitor_ =
        std::make_unique<UpdateMonitor>(source_, UpdateMonitorOptions{.debounce = std::chrono::milliseconds{10}});
    service_ = std::make_unique<UpdateStatusService>(monitor_.get());
    ASSERT_TRUE(service_->registerOn(service_connection_)) << service_->error().toStdString();
    monitor_->start();
  }

 public:
  PrivateBus bus_;
  QDBusConnection client_connection_{QStringLiteral("")};
  QDBusConnection service_connection_{QStringLiteral("")};
  std::shared_ptr<FakeUpdateSource> source_ = std::make_shared<FakeUpdateSource>();
  std::unique_ptr<UpdateMonitor> monitor_;
  std::unique_ptr<UpdateStatusService> service_;
  std::unique_ptr<UpdateStatusClient> client_;
};

TEST_F(UpdateStatusClientTest, AbsentServiceMeansUnavailableAndZero) {
  client_ = std::make_unique<UpdateStatusClient>(client_connection_);
  QSignalSpy spy(client_.get(), &UpdateStatusClient::changed);

  QTest::qWait(200);

  EXPECT_FALSE(client_->available());
  EXPECT_EQ(client_->count(), 0);
  EXPECT_TRUE(client_->state().isEmpty());
  EXPECT_EQ(spy.count(), 0);
}

TEST_F(UpdateStatusClientTest, DisconnectedBusIsTreatedAsAbsent) {
  client_ = std::make_unique<UpdateStatusClient>(QDBusConnection(QStringLiteral("not-connected")));

  EXPECT_FALSE(client_->available());
  EXPECT_EQ(client_->count(), 0);
}

TEST_F(UpdateStatusClientTest, PicksUpAServiceThatStartedBeforeAndAfterTheClient) {
  startService();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (monitor_->status().updateCount) == (5); }, kWaitMs));
  client_ = std::make_unique<UpdateStatusClient>(client_connection_);

  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->available(); }, kWaitMs));
  EXPECT_EQ(client_->count(), 5);
  EXPECT_EQ(client_->state(), "ready");
}

TEST_F(UpdateStatusClientTest, FollowsPropertiesChangedAndServiceDisappearing) {
  client_ = std::make_unique<UpdateStatusClient>(client_connection_);
  startService();
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->available() && client_->count() == 5; }, kWaitMs));

  source_->enqueue(snapshotWithCount(9));
  service_->refresh();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (client_->count()) == (9); }, kWaitMs));

  service_.reset();
  monitor_.reset();
  // disconnectFromBus only closes the connection once every QDBusConnection copy is gone.
  service_connection_ = QDBusConnection(QStringLiteral(""));
  QDBusConnection::disconnectFromBus(QStringLiteral("service-side"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !client_->available(); }, kWaitMs));
  EXPECT_EQ(client_->count(), 0);
}

TEST(UpdateStatusDiscovery, PassiveClientsLeaveAnActivatableServiceStopped) {
  QTemporaryDir files;
  ASSERT_TRUE(files.isValid());
  const QString service_name = QString::fromLatin1(UpdateStatusClient::kServiceName);
  const QString marker = files.filePath(QStringLiteral("started"));
  QFile service_file(files.filePath(service_name + QStringLiteral(".service")));
  ASSERT_TRUE(service_file.open(QIODevice::WriteOnly));
  ASSERT_GT(
      service_file.write(
          QStringLiteral("[D-BUS Service]\nName=%1\nExec=/usr/bin/touch %2\n").arg(service_name, marker).toUtf8()),
      0);
  service_file.close();
  QFile config(files.filePath(QStringLiteral("bus.conf")));
  ASSERT_TRUE(config.open(QIODevice::WriteOnly));
  ASSERT_GT(
      config.write(
          QStringLiteral(
              "<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><servicedir>%1</servicedir>"
              "<policy context='default'><allow send_destination='*'/><allow receive_sender='*'/></policy></busconfig>")
              .arg(files.path())
              .toUtf8()),
      0);
  config.close();
  PrivateBus bus;
  ASSERT_TRUE(bus.start(config.fileName())) << bus.error().toStdString();
  const QString name = QStringLiteral("passive-update-status");
  auto connection = QDBusConnection::connectToBus(bus.address(), name);
  ASSERT_TRUE(connection.isConnected());
  const auto activatable = connection.interface()->call(QStringLiteral("ListActivatableNames"));
  ASSERT_EQ(activatable.type(), QDBusMessage::ReplyMessage);
  ASSERT_TRUE(activatable.arguments().first().toStringList().contains(service_name));

  auto owner = std::make_shared<DBusOwnerTracker>(connection, service_name);
  UpdateStatusClient status(owner);
  DBusUpdateCheckClient check(owner);
  QTest::qWait(200);
  EXPECT_FALSE(status.available());
  EXPECT_FALSE(check.status().serviceReachable);
  EXPECT_FALSE(QFile::exists(marker));

  check.checkNow();
  EXPECT_TRUE(QTest::qWaitFor([&] { return QFile::exists(marker); }, kWaitMs));
  QDBusConnection::disconnectFromBus(name);
}

class HeldStatus : public QDBusVirtualObject {
 public:
  [[nodiscard]] QString introspect(const QString& /*path*/) const override { return {}; }

  bool handleMessage(const QDBusMessage& message, const QDBusConnection& /*connection*/) override {
    if (message.member() != QLatin1String("GetAll")) {
      return false;
    }
    request = message;
    return true;
  }

  static void sendProperties(const QDBusConnection& connection, int count) {
    auto signal = QDBusMessage::createSignal(QString::fromLatin1(UpdateStatusClient::kObjectPath),
                                             QStringLiteral("org.freedesktop.DBus.Properties"),
                                             QStringLiteral("PropertiesChanged"));
    signal << QString::fromLatin1(UpdateStatusClient::kInterfaceName) << QVariantMap{{QStringLiteral("Count"), count}}
           << QStringList{};
    EXPECT_TRUE(connection.send(signal));
  }

  void reply(const QDBusConnection& connection, int count) const {
    EXPECT_TRUE(connection.send(request.createReply(QVariantMap{
        {QStringLiteral("Count"), count},
        {QStringLiteral("State"), QStringLiteral("ready")},
    })));
  }

  QDBusMessage request;
};

TEST_F(UpdateStatusClientTest, ADirectHandoffDiscardsTheOldReplyAndMergesNewOwnerSignals) {
  const QString name = QString::fromLatin1(UpdateStatusClient::kServiceName);
  const QString path = QString::fromLatin1(UpdateStatusClient::kObjectPath);
  auto old = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("service-side"));
  auto next = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("new-status-owner"));
  HeldStatus first;
  HeldStatus replacement;
  ASSERT_TRUE(old.registerVirtualObject(path, &first));
  ASSERT_TRUE(next.registerVirtualObject(path, &replacement));
  ASSERT_TRUE(old.registerService(name));
  const auto queued = next.interface()->registerService(name, QDBusConnectionInterface::QueueService);
  ASSERT_TRUE(queued.isValid());
  ASSERT_EQ(queued.value(), QDBusConnectionInterface::ServiceQueued);
  client_ = std::make_unique<UpdateStatusClient>(client_connection_);
  ASSERT_TRUE(QTest::qWaitFor([&] { return first.request.type() == QDBusMessage::MethodCallMessage; }, kWaitMs));

  ASSERT_TRUE(old.unregisterService(name));
  ASSERT_TRUE(QTest::qWaitFor([&] { return replacement.request.type() == QDBusMessage::MethodCallMessage; }, kWaitMs));
  first.reply(old, 20);
  HeldStatus::sendProperties(next, 9);
  replacement.reply(next, 5);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->available() && client_->count() == 9; }, kWaitMs));
  EXPECT_EQ(client_->badgeText(), QStringLiteral("9"));
  QDBusConnection::disconnectFromBus(QStringLiteral("new-status-owner"));
}

}  // namespace
