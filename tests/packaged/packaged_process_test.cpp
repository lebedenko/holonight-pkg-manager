#include "private_bus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

namespace {

using holonight_packages_testing::PrivateBus;

constexpr int kWaitMs = 10000;
const QString kService = QStringLiteral("org.holonight.Packages1");

std::filesystem::path updatesFixture() {
  return std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "updates";
}

// Runs the real holonight-packaged binary against a private bus, a copy of the updates fixture and a throwaway HOME.
class PackagedProcessTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    ASSERT_TRUE(home_.isValid());
    const std::filesystem::path root = std::filesystem::path(home_.path().toStdString()) / "db";
    std::filesystem::create_directories(root);
    std::filesystem::copy(updatesFixture() / "local", root / "local", std::filesystem::copy_options::recursive);
    std::filesystem::copy(updatesFixture() / "sync", root / "sync", std::filesystem::copy_options::recursive);
    db_root_ = QString::fromStdString(root.string());
    client_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("process-client"));
    ASSERT_TRUE(client_.isConnected());
  }

  void TearDown() override { QDBusConnection::disconnectFromBus(QStringLiteral("process-client")); }

  std::unique_ptr<QProcess> startService() {
    auto process = std::make_unique<QProcess>();
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), bus_.address());
    environment.insert(QStringLiteral("HOME"), home_.path());
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"), home_.filePath(QStringLiteral("config")));
    environment.insert(QStringLiteral("XDG_CACHE_HOME"), home_.filePath(QStringLiteral("cache")));
    environment.insert(QStringLiteral("XDG_DATA_HOME"), home_.filePath(QStringLiteral("data")));
    process->setProcessEnvironment(environment);
    process->setProgram(QString::fromLatin1(HOLONIGHT_PACKAGED_EXECUTABLE));
    process->setArguments({QStringLiteral("--root"), db_root_, QStringLiteral("--dbpath"), db_root_,
                           QStringLiteral("--pacman-conf"),
                           QString::fromStdString((updatesFixture() / "pacman.conf").string()),
                           QStringLiteral("--debounce-ms"), QStringLiteral("20")});
    process->start();
    return process;
  }

  bool serviceOwned() {
    const auto* interface = client_.interface();
    return interface != nullptr && interface->isServiceRegistered(kService);
  }

  PrivateBus bus_;
  QTemporaryDir home_;
  QString db_root_;
  QDBusConnection client_{QStringLiteral("")};
};

TEST_F(PackagedProcessTest, ServesFixtureCountAndExitsCleanlyOnSigterm) {
  auto process = startService();
  ASSERT_TRUE(process->waitForStarted(kWaitMs));
  ASSERT_TRUE(QTest::qWaitFor([&] { return serviceOwned(); }, kWaitMs));

  QDBusInterface properties(kService, QStringLiteral("/org/holonight/Packages1"),
                            QStringLiteral("org.freedesktop.DBus.Properties"), client_);
  int count = -1;
  for (int attempt = 0; attempt < 100 && count != 3; ++attempt) {
    const QDBusMessage reply =
        properties.callWithArgumentList(QDBus::BlockWithGui, QStringLiteral("Get"),
                                        {QStringLiteral("org.holonight.Packages1.Updates"), QStringLiteral("Count")});
    count = reply.arguments().isEmpty() ? -1 : reply.arguments().first().value<QDBusVariant>().variant().toInt();
    QTest::qWait(50);
  }
  EXPECT_EQ(count, 3);

  process->terminate();
  ASSERT_TRUE(process->waitForFinished(kWaitMs));
  EXPECT_EQ(process->exitStatus(), QProcess::NormalExit);
  EXPECT_EQ(process->exitCode(), 0);
}

TEST_F(PackagedProcessTest, SecondInstanceExitsCleanlyWithoutServing) {
  auto first = startService();
  ASSERT_TRUE(first->waitForStarted(kWaitMs));
  ASSERT_TRUE(QTest::qWaitFor([&] { return serviceOwned(); }, kWaitMs));

  auto second = startService();
  ASSERT_TRUE(second->waitForFinished(kWaitMs));

  EXPECT_EQ(second->exitStatus(), QProcess::NormalExit);
  EXPECT_EQ(second->exitCode(), 0);
  EXPECT_EQ(first->state(), QProcess::Running);

  first->terminate();
  ASSERT_TRUE(first->waitForFinished(kWaitMs));
}

}  // namespace
