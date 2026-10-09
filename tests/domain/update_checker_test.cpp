#include "holonight_packages_domain/update_checker.h"

#include "holonight_packages_domain/backend_capabilities.h"
#include "holonight_packages_domain/update_snapshot_store.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <set>
#include <string>

namespace holonight_packages_domain {
namespace {

class OnlyThePort final : public UpdateChecker {
 public:
  explicit OnlyThePort(std::expected<UpdateSnapshot, UpdateCheckError> result) : result_(std::move(result)) {}
  [[nodiscard]] std::expected<UpdateSnapshot, UpdateCheckError> checkForUpdates() const override { return result_; }

 private:
  std::expected<UpdateSnapshot, UpdateCheckError> result_;
};

class OnlyTheStorePort final : public UpdateSnapshotStore {
 public:
  [[nodiscard]] std::expected<SnapshotLoad, SnapshotStoreError> load() const override { return SnapshotLoad{}; }
  [[nodiscard]] std::expected<void, SnapshotStoreError> save(const CheckedSnapshot& /*snapshot*/) override {
    return {};
  }
  [[nodiscard]] std::expected<void, SnapshotStoreError> discardInvalid() override { return {}; }
};

TEST(UpdateCheckerTest, ExactlyFourErrorCodesExist) {
  EXPECT_EQ(kAllUpdateCheckErrorCodes.size(), 4U);
  const std::set<UpdateCheckErrorCode> distinct(kAllUpdateCheckErrorCodes.begin(), kAllUpdateCheckErrorCodes.end());
  EXPECT_EQ(distinct.size(), 4U);
}

TEST(UpdateCheckerTest, EveryMessageAndNameIsNonEmptyAndUnique) {
  std::set<std::string> messages;
  std::set<std::string> names;
  for (const UpdateCheckErrorCode code : kAllUpdateCheckErrorCodes) {
    EXPECT_FALSE(updateCheckErrorMessage(code).empty());
    EXPECT_FALSE(updateCheckErrorName(code).empty());
    messages.emplace(updateCheckErrorMessage(code));
    names.emplace(updateCheckErrorName(code));
    EXPECT_EQ(UpdateCheckError{code}.message(), updateCheckErrorMessage(code));
  }
  EXPECT_EQ(messages.size(), 4U);
  EXPECT_EQ(names.size(), 4U);
  EXPECT_EQ(updateCheckErrorName(UpdateCheckErrorCode::NetworkUnavailable), "network-unavailable");
  EXPECT_EQ(updateCheckErrorName(UpdateCheckErrorCode::RepositoryUnreachable), "repository-unreachable");
  EXPECT_EQ(updateCheckErrorName(UpdateCheckErrorCode::Busy), "busy");
  EXPECT_EQ(updateCheckErrorName(UpdateCheckErrorCode::Unknown), "unknown");
}

TEST(UpdateCheckerTest, MessagesNeverMentionBackendInternals) {
  for (const UpdateCheckErrorCode code : kAllUpdateCheckErrorCodes) {
    const std::string message(updateCheckErrorMessage(code));
    EXPECT_EQ(message.find("alpm"), std::string::npos);
    EXPECT_EQ(message.find("://"), std::string::npos);
  }
}

TEST(UpdateCheckerTest, PortReturnsExactSnapshot) {
  UpdateSnapshot snapshot;
  snapshot.updates.push_back(PendingUpdate{.name = "alpha", .installedVersion = "1", .availableVersion = "2"});
  const OnlyThePort checker(snapshot);
  const auto result = checker.checkForUpdates();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, snapshot);
}

TEST(UpdateCheckerTest, PortReturnsExactError) {
  const OnlyThePort checker(std::unexpected(UpdateCheckError{UpdateCheckErrorCode::Busy}));
  const auto result = checker.checkForUpdates();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), UpdateCheckError{UpdateCheckErrorCode::Busy});
}

TEST(UpdateCheckerTest, DefaultBackendCapabilitiesCannotCheck) {
  EXPECT_FALSE(BackendCapabilities{}.canCheckForUpdates);
}

TEST(UpdateSnapshotStoreTest, PortCompilesWithReadOnlyLoad) {
  OnlyTheStorePort store;
  const UpdateSnapshotStore& readOnly = store;
  const auto loaded = readOnly.load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->state, SnapshotFileState::Absent);
  EXPECT_FALSE(loaded->snapshot.has_value());
  EXPECT_TRUE(store.save(CheckedSnapshot{}).has_value());
  EXPECT_TRUE(store.discardInvalid().has_value());
}

}  // namespace
}  // namespace holonight_packages_domain
