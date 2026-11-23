#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace holonight_packages_domain {

// One per-package ALPM action line of a transaction (transaction-history REQ-F-008).
enum class HistoryActionKind : std::uint8_t { Installed, Upgraded, Downgraded, Reinstalled, Removed };

struct HistoryAction {
  HistoryActionKind kind = HistoryActionKind::Installed;
  std::string name;
  std::string old_version;  // upgraded, downgraded, removed; else empty
  std::string new_version;  // installed, upgraded, downgraded, reinstalled; else empty

  bool operator==(const HistoryAction&) const = default;
};

// Who started a transaction (REQ-F-007).
enum class HistoryOriginKind : std::uint8_t { Packages, Pacman, Unknown };

struct HistoryOrigin {
  HistoryOriginKind kind = HistoryOriginKind::Unknown;
  std::optional<std::uint32_t> uid;  // Packages only: the caller uid of the HOLONIGHT tag
  std::string command;               // Pacman only: the text between the quotes of the Running line

  bool operator==(const HistoryOrigin&) const = default;
};

enum class HistoryStatus : std::uint8_t { Completed, Interrupted, InProgress };

enum class HistorySeverity : std::uint8_t { Warning, Error };

struct HistoryMessage {
  HistorySeverity severity = HistorySeverity::Warning;
  std::string text;  // the text after "warning: " or "error: "

  bool operator==(const HistoryMessage&) const = default;
};

// One transaction of the pacman log: from `transaction started` to `transaction completed` (REQ-F-006).
struct HistoryEntry {
  std::chrono::system_clock::time_point started;  // the start line; one-second resolution
  HistoryOrigin origin;
  std::vector<HistoryAction> actions;    // file order
  std::vector<std::string> pre_hooks;    // "<name>.hook" between the Running line and the start line
  std::vector<std::string> hooks;        // "<name>.hook" after the completed line (post-transaction)
  std::vector<HistoryMessage> messages;  // warnings and errors, never [ALPM-SCRIPTLET] output
  HistoryStatus status = HistoryStatus::Interrupted;

  bool operator==(const HistoryEntry&) const = default;
};

enum class HistoryReadMode : std::uint8_t { Full, Incremental };

// A change to the consumer's list, which it keeps in file order (oldest first).
struct HistoryReadResult {
  bool reset = false;         // discard everything, then append
  std::size_t drop_last = 0;  // 0 or 1: discard the last entry first (it changed); ignored when reset
  std::vector<HistoryEntry> append;
  // Non-empty only when the log cannot be shown (missing, unreadable, or pacman.conf unreadable); `reset` is true and
  // `append` is empty then. An empty reason with no entries is a readable log without transactions (REQ-F-021).
  std::string reason;

  bool operator==(const HistoryReadResult&) const = default;
};

// Read-only access to the transaction history in the pacman log (transaction-history DESIGN 5).
class HistorySource {
 public:
  HistorySource() = default;
  HistorySource(const HistorySource&) = default;
  HistorySource(HistorySource&&) = default;
  HistorySource& operator=(const HistorySource&) = default;
  HistorySource& operator=(HistorySource&&) = default;
  virtual ~HistorySource();

  // Blocking; intended for a worker thread. Never throws for I/O problems and never writes. Full re-reads the whole
  // log and resets the carried state; Incremental reads only the bytes after the consumed offset, and acts as Full when
  // there is no state yet, the log path or inode changed, or the file shrank. Not const: the source keeps the state.
  [[nodiscard]] virtual HistoryReadResult read(HistoryReadMode mode) = 0;
};

}  // namespace holonight_packages_domain
