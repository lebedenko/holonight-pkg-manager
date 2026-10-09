#pragma once

#include "holonight_packages_domain/update_snapshot_store.h"

#include <expected>
#include <mutex>
#include <optional>
#include <string>

namespace holonight_packages_testing {

// In-memory UpdateSnapshotStore with injectable failures and call counters.
class FakeSnapshotStore final : public holonight_packages_domain::UpdateSnapshotStore {
 public:
  using Load = holonight_packages_domain::SnapshotLoad;
  using Error = holonight_packages_domain::SnapshotStoreError;

  FakeSnapshotStore() = default;
  FakeSnapshotStore(const FakeSnapshotStore&) = delete;
  FakeSnapshotStore& operator=(const FakeSnapshotStore&) = delete;
  FakeSnapshotStore(FakeSnapshotStore&&) = delete;
  FakeSnapshotStore& operator=(FakeSnapshotStore&&) = delete;
  ~FakeSnapshotStore() override = default;

  void setStored(holonight_packages_domain::CheckedSnapshot snapshot) {
    const std::scoped_lock lock(mutex_);
    stored_ = std::move(snapshot);
    invalid_ = false;
  }
  void setInvalid() {
    const std::scoped_lock lock(mutex_);
    stored_.reset();
    invalid_ = true;
  }
  void failSaves(bool fail) {
    const std::scoped_lock lock(mutex_);
    fail_saves_ = fail;
  }

  [[nodiscard]] int saveCount() const {
    const std::scoped_lock lock(mutex_);
    return save_count_;
  }
  [[nodiscard]] int discardCount() const {
    const std::scoped_lock lock(mutex_);
    return discard_count_;
  }
  [[nodiscard]] std::optional<holonight_packages_domain::CheckedSnapshot> stored() const {
    const std::scoped_lock lock(mutex_);
    return stored_;
  }
  [[nodiscard]] bool invalid() const {
    const std::scoped_lock lock(mutex_);
    return invalid_;
  }

  [[nodiscard]] std::expected<Load, Error> load() const override {
    const std::scoped_lock lock(mutex_);
    if (stored_) {
      return Load{.snapshot = stored_, .state = holonight_packages_domain::SnapshotFileState::Valid};
    }
    return Load{
        .snapshot = std::nullopt,
        .state = invalid_ ? holonight_packages_domain::SnapshotFileState::Invalid
                          : holonight_packages_domain::SnapshotFileState::Absent,
    };
  }

  [[nodiscard]] std::expected<void, Error> save(const holonight_packages_domain::CheckedSnapshot& snapshot) override {
    const std::scoped_lock lock(mutex_);
    ++save_count_;
    if (fail_saves_) {
      return std::unexpected(Error{"injected save failure"});
    }
    stored_ = snapshot;
    invalid_ = false;
    return {};
  }

  [[nodiscard]] std::expected<void, Error> discardInvalid() override {
    const std::scoped_lock lock(mutex_);
    ++discard_count_;
    if (invalid_) {
      invalid_ = false;
    }
    return {};
  }

 private:
  mutable std::mutex mutex_;
  std::optional<holonight_packages_domain::CheckedSnapshot> stored_;
  bool invalid_ = false;
  bool fail_saves_ = false;
  int save_count_ = 0;
  int discard_count_ = 0;
};

}  // namespace holonight_packages_testing
