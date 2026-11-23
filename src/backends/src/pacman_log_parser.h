#pragma once

#include "holonight_packages_domain/history_source.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_backends {

inline constexpr std::size_t kMaxLogLineLength = std::size_t{1} << 20U;

// Carry-over state of the pacman log parser between reads (transaction-history DESIGN 5.3, D-3).
struct LogParseState {
  std::optional<holonight_packages_domain::HistoryEntry> last;  // the entry that can still change
  bool last_open = false;    // `last` has no completed line and no later start line yet
  bool attach_open = false;  // no boundary line since the start line of `last`
  // The nearest Running line since the last start line, and the hooks and messages logged after it (they belong to
  // the next transaction: pre-transaction hooks, REQ-F-009).
  std::optional<holonight_packages_domain::HistoryOrigin> pending_origin;
  std::vector<std::string> pending_hooks;
  std::vector<holonight_packages_domain::HistoryMessage> pending_messages;
  std::size_t entries_finished = 0;  // entries moved to `finished` since the state was created

  bool operator==(const LogParseState&) const = default;
};

// Parses complete pacman log lines (every line ends in '\n'; a trailing piece without one is ignored). Entries that
// can no longer change are appended to `finished` in file order; the entry that can still change stays in
// `state.last`. Malformed and unknown lines are skipped; the parse never fails (REQ-F-016). Pure: no files, no clock.
void parseLines(std::string_view lines, LogParseState& state,
                std::vector<holonight_packages_domain::HistoryEntry>& finished);

}  // namespace holonight_packages_backends
