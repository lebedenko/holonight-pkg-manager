#pragma once

#include "holonight_packages_transaction/transaction_config.h"

#include <QStringList>

#include <expected>
#include <filesystem>
#include <string>

namespace holonight_helper {

// What the helper reads from its own command line: where the root, the database directory, pacman.conf and the package
// cache are. It is the only place that builds a TransactionConfig, so nothing a bus caller sends can reach it
// (REQ-F-018). The defaults are the real system locations; tests and activation files override them.
struct HelperConfig {
  holonight_packages_transaction::TransactionConfig transaction;
  // Where the system upgrade keeps its scratch roots: the unit's StateDirectory (system-upgrade REQ-F-008).
  std::filesystem::path state_directory;
};

// `arguments` start with the program name, as QCoreApplication::arguments() does. Parses `--root`, `--dbpath`,
// `--pacman-conf`, `--cache-dir` and `--state-dir`. Every path must be absolute.
[[nodiscard]] std::expected<HelperConfig, std::string> parseHelperConfig(const QStringList& arguments);

}  // namespace holonight_helper
