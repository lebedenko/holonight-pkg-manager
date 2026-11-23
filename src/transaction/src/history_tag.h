#pragma once

#include "holonight_packages_domain/transaction_request.h"
#include "holonight_packages_transaction/log_sink.h"
#include "holonight_packages_transaction/transaction_seams.h"

#include <alpm.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace holonight_packages_transaction {

// "Running '<action> <names…>' (uid N)", or "Running 'upgrade' (uid N)" for an upgrade (transaction-history
// REQ-F-002). nullopt when the input is not a valid tag: an upgrade with names, an install or remove without names, or
// a name that fails isValidPackageName(). No free text is part of the format.
[[nodiscard]] std::optional<std::string> formatHistoryTag(holonight_packages_domain::PlanAction action,
                                                          std::span<const std::string> names, std::uint32_t uid);

enum class TagOutcome : std::uint8_t { Written, Failed, Skipped };  // Skipped: no caller uid or no valid tag

// Writes the [HOLONIGHT] line into the pacman log just before a libalpm commit (REQ-F-001). A failed write is a
// journal warning and never stops the transaction (REQ-F-004).
class HistoryTagWriter {
 public:
  HistoryTagWriter(const TransactionSeams& seams, LogSink journal);

  // Observes "tag-writing" before and "tag-written" or "tag-failed" after. A non-zero inject_fault("tag-write") skips
  // the write and counts as a failure.
  [[nodiscard]] TagOutcome write(alpm_handle_t* handle, holonight_packages_domain::PlanAction action,
                                 std::span<const std::string> names, std::optional<std::uint32_t> caller_uid) const;

 private:
  const TransactionSeams* seams_;
  LogSink journal_;
};

}  // namespace holonight_packages_transaction
