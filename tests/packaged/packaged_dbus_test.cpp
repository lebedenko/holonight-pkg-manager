#include "UpdateStatusService.h"
#include "fake_update_source.h"
#include "holonight_packages_application/update_monitor.h"
#include "private_bus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QFile>
#include <QObject>
#include <QSet>
#include <QTest>
#include <QVariantMap>
#include <QXmlStreamReader>

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
const QString kService = QString::fromLatin1(UpdateStatusService::kServiceName);
const QString kPath = QString::fromLatin1(UpdateStatusService::kObjectPath);
const QString kInterface = QString::fromLatin1(UpdateStatusService::kInterfaceName);

UpdateSnapshot snapshot(int normal, int ignored) {
  UpdateSnapshot result;
  for (int index = 0; index < normal; ++index) {
    result.updates.push_back(PendingUpdate{.name = "n" + std::to_string(index), .downloadSizeBytes = 100});
  }
  for (int index = 0; index < ignored; ++index) {
    result.updates.push_back(
        PendingUpdate{.name = "i" + std::to_string(index), .downloadSizeBytes = 900, .ignored = true});
  }
  result.dataAsOf = std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};
  return result;
}

// Records PropertiesChanged signals sent to the client connection.
class PropertiesListener : public QObject {
  Q_OBJECT

 public:
  QList<QVariantMap> changes;

 public slots:
  void onPropertiesChanged(const QString& interface_name, const QVariantMap& changed, const QStringList& /*unused*/) {
    if (interface_name == QString::fromLatin1(UpdateStatusService::kInterfaceName)) {
      changes.push_back(changed);
    }
  }
};

// Interface members as "kind name signature access", independent of document order and formatting.
QSet<QString> membersOf(const QByteArray& xml) {
  QSet<QString> members;
  QXmlStreamReader reader(xml);
  bool in_interface = false;
  QString current_method;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      const QString name = reader.name().toString();
      const auto attrs = reader.attributes();
      if (name == QLatin1String("interface")) {
        in_interface = attrs.value(QLatin1String("name")) == kInterface;
      } else if (in_interface && name == QLatin1String("property")) {
        members.insert(QStringLiteral("property %1 %2 %3")
                           .arg(attrs.value(QLatin1String("name")), attrs.value(QLatin1String("type")),
                                attrs.value(QLatin1String("access"))));
      } else if (in_interface && (name == QLatin1String("method") || name == QLatin1String("signal"))) {
        current_method = name + QLatin1Char(' ') + attrs.value(QLatin1String("name")).toString();
        members.insert(current_method);
      } else if (in_interface && name == QLatin1String("arg")) {
        members.insert(QStringLiteral("%1 arg %2 %3")
                           .arg(current_method, attrs.value(QLatin1String("type")).toString(),
                                attrs.value(QLatin1String("direction")).toString()));
      }
    } else if (reader.isEndElement() && reader.name() == QLatin1String("interface")) {
      in_interface = false;
    }
  }
  EXPECT_FALSE(reader.hasError()) << reader.errorString().toStdString();
  return members;
}

class PackagedDbusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    service_connection_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("test-service"));
    client_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("test-client"));
    ASSERT_TRUE(service_connection_.isConnected());
    ASSERT_TRUE(client_.isConnected());
    source_ = std::make_shared<FakeUpdateSource>();
    source_->enqueue(snapshot(7, 3));
    monitor_ =
        std::make_unique<UpdateMonitor>(source_, UpdateMonitorOptions{.debounce = std::chrono::milliseconds{10}});
    service_ = std::make_unique<UpdateStatusService>(monitor_.get());
    ASSERT_TRUE(service_->registerOn(service_connection_)) << service_->error().toStdString();
  }

  void TearDown() override {
    service_.reset();
    monitor_.reset();
    QDBusConnection::disconnectFromBus(QStringLiteral("test-service"));
    QDBusConnection::disconnectFromBus(QStringLiteral("test-client"));
  }

  QDBusMessage call(const QString& interface_name, const QString& method, const QVariantList& args = {}) {
    // Service and client share this thread's event loop, so the call is asynchronous and the loop is spun here.
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, interface_name, method);
    message.setArguments(args);
    QDBusPendingCall pending = client_.asyncCall(message, kWaitMs);
    QDBusPendingCallWatcher watcher(pending);
    for (int waited = 0; !watcher.isFinished() && waited < kWaitMs; waited += 5) {
      QTest::qWait(5);
    }
    EXPECT_TRUE(watcher.isFinished()) << "D-Bus call did not finish";
    return pending.reply();
  }

  QVariant property(const QString& name) {
    const QDBusMessage reply =
        call(QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"), {kInterface, name});
    EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
    return reply.arguments().isEmpty() ? QVariant{} : reply.arguments().first().value<QDBusVariant>().variant();
  }

  PrivateBus bus_;
  QDBusConnection service_connection_{QStringLiteral("")};
  QDBusConnection client_{QStringLiteral("")};
  std::shared_ptr<FakeUpdateSource> source_;
  std::unique_ptr<UpdateMonitor> monitor_;
  std::unique_ptr<UpdateStatusService> service_;
};

TEST_F(PackagedDbusTest, AllPropertiesAreReadableWithMatchingTypes) {
  monitor_->start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (monitor_->status().updateCount) == (7); }, kWaitMs));

  EXPECT_EQ(property(QStringLiteral("State")).toString(), "ready");
  EXPECT_EQ(property(QStringLiteral("Count")).toUInt(), 7U);
  EXPECT_EQ(property(QStringLiteral("IgnoredCount")).toUInt(), 3U);
  EXPECT_EQ(property(QStringLiteral("DownloadSizeBytes")).toULongLong(), 700ULL);
  EXPECT_EQ(property(QStringLiteral("DataAsOf")).toLongLong(), 1'700'000'000LL);
  EXPECT_EQ(property(QStringLiteral("LastError")).toString(), "");
  EXPECT_EQ(property(QStringLiteral("Count")).metaType().id(), QMetaType::UInt);
  EXPECT_EQ(property(QStringLiteral("DataAsOf")).metaType().id(), QMetaType::LongLong);
}

TEST_F(PackagedDbusTest, RefreshReturnsImmediatelyAndPropertiesChangedCarriesNewCount) {
  monitor_->start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (monitor_->status().updateCount) == (7); }, kWaitMs));
  PropertiesListener listener;
  ASSERT_TRUE(client_.connect(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                              QStringLiteral("PropertiesChanged"), &listener,
                              SLOT(onPropertiesChanged(QString, QVariantMap, QStringList))));
  source_->setBlocking(true);
  source_->enqueue(snapshot(2, 0));

  const QDBusMessage reply = call(kInterface, QStringLiteral("Refresh"));
  EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  EXPECT_TRUE(reply.arguments().isEmpty());
  // The evaluation is still blocked, so Refresh did not wait for it.
  EXPECT_EQ(monitor_->status().updateCount, 7);

  source_->release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !listener.changes.isEmpty(); }, kWaitMs));
  EXPECT_EQ(listener.changes.last().value(QStringLiteral("Count")).toUInt(), 2U);
}

TEST_F(PackagedDbusTest, IntrospectionMatchesCheckedInContract) {
  QFile contract(QString::fromLatin1(HOLONIGHT_PACKAGED_DBUS_XML));
  ASSERT_TRUE(contract.open(QIODevice::ReadOnly));
  const QSet<QString> expected = membersOf(contract.readAll());

  const QDBusMessage reply = call(QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
  ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  const QSet<QString> actual = membersOf(reply.arguments().first().toString().toUtf8());

  EXPECT_EQ(actual, expected);
  EXPECT_TRUE(actual.contains(QStringLiteral("method Refresh")));
  // Refresh is the only method, and it takes no arguments.
  for (const QString& member : actual) {
    if (member.startsWith(QLatin1String("method ")) || member.contains(QLatin1String(" arg "))) {
      EXPECT_EQ(member, QStringLiteral("method Refresh"));
    }
  }
}

TEST_F(PackagedDbusTest, SecondServiceCannotTakeTheName) {
  UpdateMonitor other_monitor(source_, UpdateMonitorOptions{});
  UpdateStatusService other(&other_monitor);
  QDBusConnection other_connection = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("test-other"));

  EXPECT_FALSE(other.registerOn(other_connection));
  EXPECT_FALSE(other.error().isEmpty());
  QDBusConnection::disconnectFromBus(QStringLiteral("test-other"));
}

}  // namespace

#include "packaged_dbus_test.moc"
