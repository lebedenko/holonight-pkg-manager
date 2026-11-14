#pragma once

#include "holonight_packages_transaction/log_sink.h"
#include "holonight_packages_transaction/transaction_seams.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

// Replaces the live sync databases with the scratch ones, under the live lock, at the upgrade's point of no return
// (system-upgrade REQ-F-053, REQ-F-054, REQ-F-077; SPIKES S-3). Each live file is replaced atomically: by rename,
// falling back on EXDEV (including separate mounts of the same filesystem) to a copy to `<live>/.hn-promote-<name>.tmp`
// and a rename over the live file. Signatures go before their database. On a failure before any package change every
// file already replaced is restored from the rollback directory and a file that was added is removed. Only the upgrade
// driver may promote (rule H-013).
class SyncPromotion {
 public:
  SyncPromotion(std::filesystem::path scratch_sync, std::filesystem::path live_sync, std::filesystem::path rollback,
                const TransactionSeams& seams, LogSink journal);

  // True when every live file equals its scratch counterpart afterwards; false after a rollback.
  [[nodiscard]] bool promote();

 private:
  struct Step {
    std::string name;
    bool remove = false;  // a live .db.sig the scratch side no longer has
  };
  [[nodiscard]] std::optional<std::vector<Step>> plan() const;
  [[nodiscard]] bool replace(const std::string& name) const;
  void rollBack(const std::vector<std::string>& replaced, const std::vector<std::string>& added) const;

  std::filesystem::path scratch_;
  std::filesystem::path live_;
  std::filesystem::path rollback_;
  const TransactionSeams* seams_;
  LogSink journal_;
};

// Removes the temporary files of a promotion that a killed helper left in the live sync directory (REQ-F-077).
void removePromotionLeftovers(const std::filesystem::path& live_sync) noexcept;

}  // namespace holonight_packages_transaction
