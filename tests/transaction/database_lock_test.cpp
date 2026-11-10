#include "holonight_packages_transaction/database_lock.h"

#include "alpm_session.h"
#include "fixture_root.h"

#include <QProcess>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::PlanAction;
using holonight_transaction_testing::FixtureRoot;
namespace fs = std::filesystem;

TEST(DatabaseLockTest, PreviewAndFailedAcquisitionDoNotClearAnotherSessionsOwnership) {
  const FixtureRoot world;
  world.writeConf(std::vector<std::string>{});
  auto owner = AlpmSession::open(world.config(), PlanAction::Remove, {});
  auto preview = AlpmSession::open(world.config(), PlanAction::Remove, {});
  auto contender = AlpmSession::open(world.config(), PlanAction::Remove, {});
  ASSERT_TRUE(owner);
  ASSERT_TRUE(preview);
  ASSERT_TRUE(contender);
  ASSERT_TRUE((*owner)->beginTransaction(0));
  EXPECT_FALSE((*contender)->beginTransaction(0));
  ASSERT_TRUE((*preview)->beginTransaction(ALPM_TRANS_FLAG_NOLOCK));
  (*preview)->releaseTransaction();
  preview->reset();
  contender->reset();
  EXPECT_TRUE(DatabaseLockGuard{}.ownsLock());
  EXPECT_TRUE(fs::exists(world.lockPath()));
  (*owner)->releaseTransaction();
  EXPECT_FALSE(DatabaseLockGuard{}.ownsLock());
  EXPECT_FALSE(fs::exists(world.lockPath()));
}

class DatabaseFinalizationTest : public ::testing::TestWithParam<const char*> {};

TEST_P(DatabaseFinalizationTest, SerializesLifecycleAndPreservesForeignReplacement) {
  const FixtureRoot world;
  world.writeConf(std::vector<std::string>{});
  const std::string mode(GetParam());
  if (mode == "foreign") {
    std::ofstream(world.lockPath()) << "foreign";
  }
  QProcess child;
  child.start(QStringLiteral(HOLONIGHT_LOCK_CHILD),
              {QString::fromStdString(mode), QString::fromStdString(world.base().string())});
  ASSERT_TRUE(child.waitForStarted(3000));
  auto line = [&child] {
    while (!child.canReadLine()) {
      if (!child.waitForReadyRead(3000)) {
        return QByteArray{};
      }
    }
    return child.readLine().trimmed();
  };
  auto proceed = [&child] {
    child.write("continue\n");
    return child.waitForBytesWritten(3000);
  };
  ASSERT_EQ(line(), "ready");
  // The reply grace window must leave the worker's lock intact.
  EXPECT_EQ(fs::exists(world.lockPath()), mode != "init-blocked");
  if (mode == "missing") {
    fs::remove(world.lockPath());
  }
  ASSERT_TRUE(proceed());
  if (mode == "released") {
    ASSERT_EQ(line(), "released");
    EXPECT_FALSE(fs::exists(world.lockPath()));
    std::ofstream(world.lockPath()) << "replacement";
    ASSERT_TRUE(proceed());
  }
  ASSERT_EQ(line(), mode == "release-blocked" || mode == "teardown-blocked" || mode == "init-blocked" ? "blocked"
                                                                                                      : "finalized");
  const bool preserved = mode == "foreign" || mode == "released";
  EXPECT_EQ(fs::exists(world.lockPath()), preserved);
  if (mode == "release-blocked" || mode == "teardown-blocked" || mode == "init-blocked") {
    std::ofstream(world.lockPath()) << "replacement";
  }
  ASSERT_TRUE(proceed());
  ASSERT_TRUE(child.waitForFinished(3000));
  EXPECT_EQ(child.exitStatus(), QProcess::NormalExit);
  EXPECT_EQ(child.exitCode(), 0);
  EXPECT_EQ(fs::exists(world.lockPath()),
            preserved || mode == "release-blocked" || mode == "teardown-blocked" || mode == "init-blocked");
}

INSTANTIATE_TEST_SUITE_P(Races, DatabaseFinalizationTest,
                         ::testing::Values("released", "release-blocked", "init-blocked", "owned", "foreign", "missing",
                                           "acquired-worker", "teardown-blocked"));

}  // namespace
}  // namespace holonight_packages_transaction
