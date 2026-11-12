#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace holonight_packages_domain {

// REQ-F-049. `Hooks` can occur twice: PreTransaction hooks run before `Installing`/`Removing`, PostTransaction hooks
// after it (SI-17). `current` never decreases within one occurrence of a phase.
// The system upgrade appends `Syncing` (preparation, one per repository), `Promoting`, and the step markers
// `KeyringStep` and `MainStep`, whose `current` and `total` are the step index and count (system-upgrade DESIGN 6.3).
enum class ProgressPhase : std::uint8_t {
  Resolving,
  Downloading,
  Verifying,
  Hooks,
  Installing,
  Removing,
  Done,
  Syncing,
  Promoting,
  KeyringStep,
  MainStep,
};

[[nodiscard]] std::string_view progressPhaseName(ProgressPhase phase) noexcept;
// The inverse of progressPhaseName; empty for any other string.
[[nodiscard]] std::optional<ProgressPhase> parseProgressPhase(std::string_view name) noexcept;

struct ProgressUpdate {
  ProgressPhase phase = ProgressPhase::Resolving;
  std::uint32_t current = 0;
  std::uint32_t total = 0;
  std::string item;  // package, file or hook name; empty when the phase has no item
  // The upgrade step the update belongs to, kept by the client from the last step marker; 0 outside an upgrade.
  std::uint8_t step_index = 0;
  std::uint8_t step_count = 0;

  bool operator==(const ProgressUpdate&) const = default;
};

// Called on the worker thread that runs the transaction.
using ProgressSink = std::function<void(const ProgressUpdate&)>;

}  // namespace holonight_packages_domain
