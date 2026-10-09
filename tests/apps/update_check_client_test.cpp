#include "DBusUpdateCheckClient.h"
#include "UpdateStatusService.h"
#include "fake_clock.h"
#include "fake_snapshot_store.h"
#include "fake_update_checker.h"
#include "fake_update_source.h"
#include "holonight_packages_application/update_check_service.h"
#include "holonight_packages_application/update_monitor.h"
#include "private_bus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>

namespace {

using holonight_packages_application::UpdateCheckPolicy;
using holonight_packages_application::UpdateCheckService;
using holonight_packages_application::UpdateMonitor;
using holonight_packages_application::UpdateMonitorOptions;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakeSnapshotStore;
using holonight_packages_testing::FakeUpdateChecker;
using holonight_packages_testing::FakeUpdateSource;
using holonight_packages_testing::PrivateBus;

constexpr int kWaitMs = 5000;

class UpdateCheckClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    clientConnection_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("check-client-side"));
    ASSERT_TRUE(clientConnection_.isConnected());
  }
  void TearDown() override {
    client_.reset();
    service_.reset();
    check_.reset();
    monitor_.reset();
    QDBusConnection::disconnectFromBus(QStringLiteral("check-service-side"));
    QDBusConnection::disconnectFromBus(QStringLiteral("check-client-side"));
  }

  void startService(bool capable = true) {
    serviceConnection_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("check-service-side"));
    monitor_ = std::make_unique<UpdateMonitor>(source_, UpdateMonitorOptions{});
    check_ = std::make_unique<UpdateCheckService>(
        UpdateCheckService::Dependencies{
            .checker = checker_,
            .store = store_,
            .clock = clock_,
            .capabilities = {.canCheckForUpdates = capable},
        },
        UpdateCheckPolicy{});
    service_ = std::make_unique<UpdateStatusService>(monitor_.get(), check_.get());
    ASSERT_TRUE(service_->registerOn(serviceConnection_)) << service_->error().toStdString();
  }

  PrivateBus bus_;
  QDBusConnection clientConnection_{QStringLiteral("")};
  QDBusConnection serviceConnection_{QStringLiteral("")};
  std::shared_ptr<FakeUpdateSource> source_ = std::make_shared<FakeUpdateSource>();
  std::shared_ptr<FakeUpdateChecker> checker_ = std::make_shared<FakeUpdateChecker>();
  std::shared_ptr<FakeSnapshotStore> store_ = std::make_shared<FakeSnapshotStore>();
  std::shared_ptr<FakeClock> clock_ = std::make_shared<FakeClock>();
  std::unique_ptr<UpdateMonitor> monitor_;
  std::unique_ptr<UpdateCheckService> check_;
  std::unique_ptr<UpdateStatusService> service_;
  std::unique_ptr<DBusUpdateCheckClient> client_;
};

TEST_F(UpdateCheckClientTest, CheckNowMessageEnablesBusActivation) {
  const QDBusMessage message = DBusUpdateCheckClient::checkNowMessage();
  EXPECT_TRUE(message.autoStartService());
  EXPECT_EQ(message.member(), "CheckNow");
  EXPECT_EQ(message.interface(), "org.holonight.Packages1.UpdateCheck");
  EXPECT_TRUE(message.arguments().isEmpty());
}

TEST_F(UpdateCheckClientTest, AbsentServiceIsUnreachableAndSilent) {
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  QSignalSpy changed(client_.get(), &UpdateCheckClient::statusChanged);
  QTest::qWait(150);
  EXPECT_FALSE(client_->status().serviceReachable);
  EXPECT_EQ(changed.count(), 0);
}

TEST_F(UpdateCheckClientTest, MirrorsThePropertiesTheServicePublishes) {
  startService();
  checker_->enqueue(holonight_packages_domain::UpdateSnapshot{});
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->status().serviceReachable; }, kWaitMs));
  EXPECT_TRUE(client_->status().canCheck);
  EXPECT_FALSE(client_->status().checking);
  EXPECT_EQ(client_->status().snapshotFetchedAt, 0);

  check_->requestCheck(holonight_packages_application::CheckOrigin::OnDemand);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->status().snapshotFetchedAt != 0 && !client_->status().checking; },
                              kWaitMs));
  const auto& status = check_->status();
  EXPECT_EQ(client_->status().lastCheckTime,
            std::chrono::duration_cast<std::chrono::seconds>(status.lastCheckTime.time_since_epoch()).count());
  EXPECT_EQ(client_->status().snapshotFetchedAt,
            std::chrono::duration_cast<std::chrono::seconds>(status.snapshotFetchedAt->time_since_epoch()).count());
  EXPECT_TRUE(client_->status().lastCheckSucceeded);
  EXPECT_TRUE(client_->status().lastCheckError.isEmpty());
}

TEST_F(UpdateCheckClientTest, ReportsCanCheckFalseForANonCapableBackend) {
  startService(false);
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->status().serviceReachable; }, kWaitMs));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !client_->status().canCheck; }, kWaitMs));
}

TEST_F(UpdateCheckClientTest, CheckNowIssuesExactlyOneCallAndEmitsCheckCompleted) {
  startService();
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->status().serviceReachable; }, kWaitMs));
  QSignalSpy completed(client_.get(), &UpdateCheckClient::checkCompleted);
  QSignalSpy failed(client_.get(), &UpdateCheckClient::checkNowFailed);

  client_->checkNow();

  ASSERT_TRUE(QTest::qWaitFor([&] { return completed.count() >= 1; }, kWaitMs));
  QTest::qWait(100);
  EXPECT_EQ(checker_->callCount(), 1);
  EXPECT_EQ(completed.count(), 1);
  EXPECT_EQ(failed.count(), 0);
}

TEST_F(UpdateCheckClientTest, CooldownNoOpEmitsSuccessfulAcknowledgementWithoutCompletion) {
  startService();
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->status().serviceReachable; }, kWaitMs));
  QSignalSpy acknowledged(client_.get(), &UpdateCheckClient::checkNowSucceeded);
  QSignalSpy completed(client_.get(), &UpdateCheckClient::checkCompleted);
  client_->checkNow();
  ASSERT_TRUE(QTest::qWaitFor([&] { return acknowledged.count() == 1 && completed.count() == 1; }, kWaitMs));
  client_->checkNow();
  ASSERT_TRUE(QTest::qWaitFor([&] { return acknowledged.count() == 2; }, kWaitMs));
  QTest::qWait(100);
  EXPECT_EQ(completed.count(), 1);
  EXPECT_EQ(checker_->callCount(), 1);
}

TEST_F(UpdateCheckClientTest, MissingNameEmitsCheckNowFailedOnce) {
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  QSignalSpy failed(client_.get(), &UpdateCheckClient::checkNowFailed);

  client_->checkNow();

  ASSERT_TRUE(QTest::qWaitFor([&] { return failed.count() >= 1; }, kWaitMs));
  QTest::qWait(100);
  EXPECT_EQ(failed.count(), 1);
}

TEST_F(UpdateCheckClientTest, ErrorReplyEmitsCheckNowFailedOnce) {
  // A service that owns the name but exports no UpdateCheck interface answers with an error.
  serviceConnection_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("check-service-side"));
  QObject empty;
  ASSERT_TRUE(serviceConnection_.registerObject(QStringLiteral("/org/holonight/Packages1"), &empty));
  ASSERT_TRUE(serviceConnection_.registerService(QStringLiteral("org.holonight.Packages1")));
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  QSignalSpy failed(client_.get(), &UpdateCheckClient::checkNowFailed);

  client_->checkNow();

  ASSERT_TRUE(QTest::qWaitFor([&] { return failed.count() >= 1; }, kWaitMs));
  QTest::qWait(100);
  EXPECT_EQ(failed.count(), 1);
  serviceConnection_.unregisterObject(QStringLiteral("/org/holonight/Packages1"));
}

TEST_F(UpdateCheckClientTest, ServiceDisappearingClearsCheckingAndMarksItUnreachable) {
  startService();
  checker_->setBlocking(true);
  client_ = std::make_unique<DBusUpdateCheckClient>(clientConnection_);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->status().serviceReachable; }, kWaitMs));
  check_->requestCheck(holonight_packages_application::CheckOrigin::OnDemand);
  ASSERT_TRUE(QTest::qWaitFor([&] { return client_->status().checking; }, kWaitMs));

  checker_->release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !check_->status().checking; }, kWaitMs));
  service_.reset();
  check_.reset();
  monitor_.reset();
  serviceConnection_ = QDBusConnection(QStringLiteral(""));  // drop our handle so the connection really closes
  QDBusConnection::disconnectFromBus(QStringLiteral("check-service-side"));

  ASSERT_TRUE(QTest::qWaitFor([&] { return !client_->status().serviceReachable; }, kWaitMs));
  EXPECT_FALSE(client_->status().checking);
}

}  // namespace
