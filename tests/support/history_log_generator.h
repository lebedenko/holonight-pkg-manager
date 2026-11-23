#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

namespace holonight_test {

// The shape of the REQ-NF-001 fixture: about 5 MiB, 60,000 lines and 1,178 transactions (transaction-history
// DESIGN 10.1).
struct HistoryLogShape {
  std::size_t transactions = 1178;
  std::size_t lines = 60000;
  std::uint64_t seed = 0x484953;
};

struct GeneratedHistoryLog {
  std::string text;
  std::size_t lines = 0;
  std::size_t transactions = 0;
};

// A deterministic pacman log: per transaction an optional Running line, pre-transaction hooks, a start line, 1 to 40
// actions of the five kinds, scriptlet output, a completed line and post-transaction hooks. Scriptlet lines fill the
// log up to exactly `shape.lines` lines.
[[nodiscard]] GeneratedHistoryLog generateHistoryLog(const HistoryLogShape& shape = {});

// One complete transaction of about 1 KiB that starts after every line of generateHistoryLog() (REQ-NF-002).
[[nodiscard]] std::string generatedTransaction(std::mt19937_64& random, std::size_t index);

}  // namespace holonight_test
