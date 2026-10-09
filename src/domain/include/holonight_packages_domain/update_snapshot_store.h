#pragma once

#include "holonight_packages_domain/update_checker.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace holonight_packages_domain {

struct SnapshotStoreError {
  // Developer-facing; logged only.
  std::string message;
};

// Invalid: corrupt, truncated, unknown schema version or count mismatch.
enum class SnapshotFileState : std::uint8_t { Absent, Valid, Invalid };

struct SnapshotLoad {
  // Set if and only if state is Valid.
  std::optional<CheckedSnapshot> snapshot;
  SnapshotFileState state = SnapshotFileState::Absent;
};

class UpdateSnapshotStore {
 public:
  UpdateSnapshotStore() = default;
  UpdateSnapshotStore(const UpdateSnapshotStore&) = default;
  UpdateSnapshotStore(UpdateSnapshotStore&&) = default;
  UpdateSnapshotStore& operator=(const UpdateSnapshotStore&) = default;
  UpdateSnapshotStore& operator=(UpdateSnapshotStore&&) = default;
  virtual ~UpdateSnapshotStore();

  // READ-ONLY, safe in any process. Absent gives {nullopt, Absent}; a corrupt, truncated or unknown-version file
  // gives {nullopt, Invalid}. Never modifies or deletes the file. An I/O error that prevents reading is an error.
  [[nodiscard]] virtual std::expected<SnapshotLoad, SnapshotStoreError> load() const = 0;
  // WRITER ONLY. Atomic: temporary file in the same directory, then rename. On failure the previous file is intact.
  [[nodiscard]] virtual std::expected<void, SnapshotStoreError> save(const CheckedSnapshot& snapshot) = 0;
  // WRITER ONLY. Removes the file if it is still invalid (re-checked inside the call).
  [[nodiscard]] virtual std::expected<void, SnapshotStoreError> discardInvalid() = 0;
};

}  // namespace holonight_packages_domain
