#pragma once

#include "holonight_packages_domain/update_snapshot_store.h"

#include <filesystem>
#include <functional>

namespace holonight_packages_persistence {

struct JsonUpdateSnapshotStoreHooks {
  // Fault injection for tests: runs after the temporary file is complete and before the rename. Returning false
  // makes save() fail at that point.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::function<bool()> beforeRename;
};

// Stores one CheckedSnapshot as a JSON document. load() is read-only and safe in any process; save() and
// discardInvalid() belong to the single writer (holonight-packaged).
class JsonUpdateSnapshotStore final : public holonight_packages_domain::UpdateSnapshotStore {
 public:
  static constexpr int kSchemaVersion = 1;
  static constexpr std::uintmax_t kMaxFileBytes = 8ULL * 1024 * 1024;

  explicit JsonUpdateSnapshotStore(std::filesystem::path file, JsonUpdateSnapshotStoreHooks hooks = {});

  [[nodiscard]] std::optional<holonight_packages_domain::CheckHistory> loadHistory() const override;
  [[nodiscard]] std::expected<void, holonight_packages_domain::SnapshotStoreError> saveHistory(
      const holonight_packages_domain::CheckHistory& history) override;

  [[nodiscard]] const std::filesystem::path& file() const { return file_; }

  [[nodiscard]] std::expected<holonight_packages_domain::SnapshotLoad, holonight_packages_domain::SnapshotStoreError>
  load() const override;
  [[nodiscard]] std::expected<void, holonight_packages_domain::SnapshotStoreError> save(
      const holonight_packages_domain::CheckedSnapshot& snapshot) override;
  [[nodiscard]] std::expected<void, holonight_packages_domain::SnapshotStoreError> discardInvalid() override;

 private:
  std::filesystem::path file_;
  JsonUpdateSnapshotStoreHooks hooks_;
};

}  // namespace holonight_packages_persistence
