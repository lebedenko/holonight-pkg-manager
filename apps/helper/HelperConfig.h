#pragma once

#include "holonight_packages_transaction/transaction_config.h"

#include <QStringList>

#include <expected>
#include <string>

namespace holonight_helper {

// What the helper reads from its own command line: where the root, the database directory, pacman.conf and the package
// cache are. It is the only place that builds a TransactionConfig, so nothing a bus caller sends can reach it
// (REQ-F-018). The defaults are the real system locations; tests and activation files override them.
struct HelperConfig {
  holonight_packages_transaction::TransactionConfig transaction;
  // libalpm keeps its lock file at <database directory>/db.lck.
  [[nodiscard]] std::string lockFile() const;
};

// `arguments` start with the program name, as QCoreApplication::arguments() does. Parses `--root`, `--dbpath`,
// `--pacman-conf` and `--cache-dir`. Every path must be absolute.
[[nodiscard]] std::expected<HelperConfig, std::string> parseHelperConfig(const QStringList& arguments);

}  // namespace holonight_helper
