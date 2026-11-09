#include "event_mapper.h"

#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vector>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using namespace std::chrono_literals;

class EventMapperTest : public ::testing::Test {
 public:
  EventMapper make(std::size_t installs = 2, std::size_t removals = 0) {
    return {[this](const ProgressUpdate& update) { updates.push_back(update); }, installs, removals,
            [this] { return now; }};
  }
  static alpm_event_t event(alpm_event_type_t type) {
    alpm_event_t value{};
    value.type = type;
    return value;
  }
  static alpm_event_t hookRun(const char* name, std::size_t position, std::size_t total) {
    alpm_event_t value = event(ALPM_EVENT_HOOK_RUN_START);
    value.hook_run.name = name;
    value.hook_run.position = position;
    value.hook_run.total = total;
    return value;
  }

  std::vector<ProgressUpdate> updates;
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::time_point{} + 1h;
};

TEST_F(EventMapperTest, ResolvingEventsAreReportedWithoutCounts) {
  EventMapper mapper = make();
  mapper.onEvent(event(ALPM_EVENT_RESOLVEDEPS_START));
  ASSERT_EQ(updates.size(), 1U);
  EXPECT_EQ(updates[0], (ProgressUpdate{.phase = ProgressPhase::Resolving, .current = 0, .total = 0, .item = {}}));
}

TEST_F(EventMapperTest, RetrievalCountsCompletedPackageFilesAndIgnoresSignatureFiles) {
  EventMapper mapper = make();
  alpm_event_t start = event(ALPM_EVENT_PKG_RETRIEVE_START);
  start.pkg_retrieve.num = 2;
  mapper.onEvent(start);
  alpm_download_event_completed_t completed{.total = 10, .result = 0};
  mapper.onDownload("a-1.0-1-any.pkg.tar.zst", ALPM_DOWNLOAD_COMPLETED, &completed);
  mapper.onDownload("a-1.0-1-any.pkg.tar.zst.sig", ALPM_DOWNLOAD_COMPLETED, &completed);
  mapper.onDownload("b-1.0-1-any.pkg.tar.zst", ALPM_DOWNLOAD_COMPLETED, &completed);
  ASSERT_EQ(updates.size(), 3U);
  EXPECT_EQ(updates[0], (ProgressUpdate{.phase = ProgressPhase::Downloading, .current = 0, .total = 2, .item = {}}));
  EXPECT_EQ(updates[1],
            (ProgressUpdate{
                .phase = ProgressPhase::Downloading, .current = 1, .total = 2, .item = "a-1.0-1-any.pkg.tar.zst"}));
  EXPECT_EQ(updates[2],
            (ProgressUpdate{
                .phase = ProgressPhase::Downloading, .current = 2, .total = 2, .item = "b-1.0-1-any.pkg.tar.zst"}));
}

TEST_F(EventMapperTest, UpdatesWithinOneHundredMillisecondsOfTheSamePhaseAreCoalesced) {
  EventMapper mapper = make();
  alpm_event_t start = event(ALPM_EVENT_PKG_RETRIEVE_START);
  start.pkg_retrieve.num = 1;
  mapper.onEvent(start);
  alpm_download_event_progress_t progress{.downloaded = 1, .total = 10};
  for (int i = 0; i < 5; ++i) {
    now += 10ms;
    mapper.onDownload("a-1.0-1-any.pkg.tar.zst", ALPM_DOWNLOAD_PROGRESS, &progress);
  }
  EXPECT_EQ(updates.size(), 1U);  // only the phase start went through
  now += 100ms;
  mapper.onDownload("a-1.0-1-any.pkg.tar.zst", ALPM_DOWNLOAD_PROGRESS, &progress);
  EXPECT_EQ(updates.size(), 2U);
}

TEST_F(EventMapperTest, PhaseChangesAndCompletionsAreNeverCoalesced) {
  EventMapper mapper = make();
  mapper.onEvent(event(ALPM_EVENT_RESOLVEDEPS_START));
  mapper.onEvent(event(ALPM_EVENT_INTEGRITY_START));
  mapper.onEvent(hookRun("10-a.hook", 1, 2));
  mapper.onEvent(hookRun("20-b.hook", 2, 2));  // a forced update inside the phase
  ASSERT_EQ(updates.size(), 4U);
  EXPECT_EQ(updates[1].phase, ProgressPhase::Verifying);
  EXPECT_EQ(updates[2], (ProgressUpdate{.phase = ProgressPhase::Hooks, .current = 1, .total = 2, .item = "10-a.hook"}));
  EXPECT_EQ(updates[3], (ProgressUpdate{.phase = ProgressPhase::Hooks, .current = 2, .total = 2, .item = "20-b.hook"}));
}

TEST_F(EventMapperTest, CurrentNeverDecreasesWithinOneOccurrenceOfAPhase) {
  EventMapper mapper = make();
  mapper.onEvent(hookRun("30-c.hook", 3, 3));
  mapper.onEvent(hookRun("10-a.hook", 1, 3));
  ASSERT_EQ(updates.size(), 2U);
  EXPECT_EQ(updates[1].current, 3U);
}

TEST_F(EventMapperTest, HooksMayOccurTwiceAndTheCountRestartsWithTheSecondOccurrence) {
  EventMapper mapper = make();
  mapper.onEvent(hookRun("pre.hook", 2, 2));
  alpm_event_t operation = event(ALPM_EVENT_PACKAGE_OPERATION_START);
  operation.package_operation.operation = ALPM_PACKAGE_INSTALL;
  mapper.onEvent(operation);
  mapper.onEvent(hookRun("post.hook", 1, 1));
  ASSERT_EQ(updates.size(), 3U);
  EXPECT_EQ(updates[0].phase, ProgressPhase::Hooks);
  EXPECT_EQ(updates[1].phase, ProgressPhase::Installing);
  EXPECT_EQ(updates[2], (ProgressUpdate{.phase = ProgressPhase::Hooks, .current = 1, .total = 1, .item = "post.hook"}));
}

TEST_F(EventMapperTest, RemovalsAreReportedAsRemovingWithTheRemovalTotal) {
  EventMapper mapper = make(0, 3);
  alpm_event_t operation = event(ALPM_EVENT_PACKAGE_OPERATION_START);
  operation.package_operation.operation = ALPM_PACKAGE_REMOVE;
  mapper.onEvent(operation);
  ASSERT_EQ(updates.size(), 1U);
  EXPECT_EQ(updates[0].phase, ProgressPhase::Removing);
  EXPECT_EQ(updates[0].total, 3U);
}

TEST_F(EventMapperTest, DoneIsAlwaysLastAndNothingFollowsIt) {
  EventMapper mapper = make();
  mapper.onEvent(event(ALPM_EVENT_RESOLVEDEPS_START));
  mapper.finish();
  mapper.onEvent(event(ALPM_EVENT_INTEGRITY_START));
  mapper.finish();
  ASSERT_EQ(updates.size(), 2U);
  EXPECT_EQ(updates.back(), (ProgressUpdate{.phase = ProgressPhase::Done, .current = 1, .total = 1, .item = {}}));
}

TEST_F(EventMapperTest, WithoutASinkNothingHappens) {
  EventMapper mapper(nullptr, 1, 0);
  mapper.onEvent(event(ALPM_EVENT_RESOLVEDEPS_START));
  mapper.finish();
  SUCCEED();
}

}  // namespace
}  // namespace holonight_packages_transaction
