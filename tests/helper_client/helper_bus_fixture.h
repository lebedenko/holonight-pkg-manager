#pragma once

#include "fake_helper_object.h"
#include "private_bus.h"

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>

namespace holonight_packages_testing {

// Starts a private dbus-daemon for each test. If the daemon cannot start the test fails with a remediation message,
// so a missing dbus-daemon can never make a test in this binary pass (REQ-NF-006). The system bus is never used
// (REQ-NF-005): every connection is made to the private address.
class HelperBusFixture : public testing::Test {
 public:
  void SetUp() override {
    helper_wire::registerHelperWireTypes();
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    ++counter_;
  }

  void TearDown() override {
    fake_.reset();
    for (const QString& name : opened_) {
      QDBusConnection::disconnectFromBus(name);
    }
  }

  // A new connection to the private bus.
  QDBusConnection connect(const QString& label) {
    const QString name = QStringLiteral("helper-client-test-%1-%2").arg(label).arg(++counter_);
    opened_.push_back(name);
    return QDBusConnection::connectToBus(bus_.address(), name);
  }

  // Starts the scripted fake helper on its own connection and owns the helper's well-known name.
  FakeHelperObject& startFakeHelper() {
    helper_connection_ = std::make_unique<QDBusConnection>(connect(QStringLiteral("fake")));
    helper_name_ = opened_.back();
    fake_ = std::make_unique<FakeHelperObject>(*helper_connection_);
    EXPECT_TRUE(fake_->start());
    return *fake_;
  }

  // Calls a method on the (fake) helper and pumps the event loop until the reply arrives.
  static QDBusMessage call(QDBusConnection& from, const QString& method, const QVariantList& arguments,
                           int timeout_ms = 5000) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        FakeHelperObject::serviceName(), FakeHelperObject::servicePath(), FakeHelperObject::interfaceName(), method);
    message.setArguments(arguments);
    QDBusPendingCallWatcher watcher(from.asyncCall(message, timeout_ms));
    bool finished = false;
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, [&finished] { finished = true; });
    EXPECT_TRUE(QTest::qWaitFor([&finished] { return finished; }, timeout_ms + 1000));
    return watcher.reply();
  }

  PrivateBus bus_;
  std::unique_ptr<QDBusConnection> helper_connection_;
  std::unique_ptr<FakeHelperObject> fake_;
  QString helper_name_;
  QStringList opened_;
  static inline int counter_ = 0;
};

}  // namespace holonight_packages_testing
