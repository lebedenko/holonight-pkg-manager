#include "holonight_packages_domain/history_source.h"

#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace holonight_packages_domain;

// A double that implements only the port: it proves the header needs no Qt, libalpm or file type (REQ-F-033).
class PortOnlyHistorySource final : public HistorySource {
 public:
  HistoryReadResult read(HistoryReadMode mode) override {
    modes.push_back(mode);
    HistoryReadResult result;
    result.reset = mode == HistoryReadMode::Full;
    result.append.push_back(HistoryEntry{});
    return result;
  }

  std::vector<HistoryReadMode> modes;
};

TEST(HistorySource, PortIsImplementableWithoutTransportTypes) {
  PortOnlyHistorySource source;
  HistorySource& port = source;

  const HistoryReadResult full = port.read(HistoryReadMode::Full);
  const HistoryReadResult incremental = port.read(HistoryReadMode::Incremental);

  EXPECT_TRUE(full.reset);
  EXPECT_FALSE(incremental.reset);
  EXPECT_EQ(full.append.size(), 1U);
  EXPECT_EQ(source.modes, (std::vector{HistoryReadMode::Full, HistoryReadMode::Incremental}));
}

TEST(HistorySource, DefaultEntryIsAnInterruptedTransactionOfUnknownOrigin) {
  const HistoryEntry entry;
  EXPECT_EQ(entry.status, HistoryStatus::Interrupted);
  EXPECT_EQ(entry.origin.kind, HistoryOriginKind::Unknown);
  EXPECT_FALSE(entry.origin.uid.has_value());
  EXPECT_TRUE(entry.actions.empty());
}

TEST(HistorySource, ReadResultsCompareByValue) {
  HistoryReadResult left;
  left.drop_last = 1;
  left.append.push_back(HistoryEntry{
      .started = {},
      .origin = {.kind = HistoryOriginKind::Packages, .uid = 1000, .command = {}},
      .actions = {},
      .pre_hooks = {},
      .hooks = {"20-systemd-sysusers.hook"},
      .messages = {},
      .status = HistoryStatus::Completed,
  });
  HistoryReadResult right = left;
  EXPECT_EQ(left, right);
  right.append.front().hooks.clear();
  EXPECT_NE(left, right);
}

}  // namespace
