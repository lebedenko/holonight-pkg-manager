#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace holonight_packages_persistence {
TEST(CheckHistory, RestartPreservesCompletedFailureIndependentlyOfSnapshot) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto path = std::filesystem::path(directory.path().toStdString()) / "updates.json";
  JsonUpdateSnapshotStore store(path);
  const auto completed = std::chrono::system_clock::time_point{std::chrono::seconds{1234}};
  ASSERT_TRUE(store.saveHistory({.completed = completed,
                                 .succeeded = false,
                                 .error = holonight_packages_domain::UpdateCheckErrorCode::NetworkUnavailable}));
  JsonUpdateSnapshotStore restarted(path);
  const auto history = restarted.loadHistory();
  ASSERT_TRUE(history);
  EXPECT_EQ(history->completed, completed);
  EXPECT_FALSE(history->succeeded);
  EXPECT_EQ(history->error, holonight_packages_domain::UpdateCheckErrorCode::NetworkUnavailable);
  EXPECT_EQ(restarted.load()->state, holonight_packages_domain::SnapshotFileState::Absent);
}
}  // namespace holonight_packages_persistence
