#include "UpdateStatusClient.h"
#include "UpdateStatusService.h"
#include "fake_update_source.h"
#include "holonight_packages_application/update_monitor.h"
#include "private_bus.h"

#include <QDBusConnection>
#include <QSignalSpy>
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

}  // namespace
