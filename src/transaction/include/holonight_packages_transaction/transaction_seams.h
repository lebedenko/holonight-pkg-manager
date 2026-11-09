#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace holonight_packages_transaction {

// Hooks for tests. Production code never sets them: main.cpp of the helper does not reference this header (H-008).
struct TransactionSeams {
  // Called at named points of an execution, on the thread that runs it:
  //   "opened"       the libalpm handle is open
  //   "resolved"     the re-resolution finished and holds the database lock
  //   "downloading"  libalpm starts to retrieve packages (inside the commit, before the commit point)
  //   "commit-point" libalpm reached the first PreTransaction hook or the transaction start
  //   "committed"    the commit gate was passed: a cancel is now refused (REQ-F-058)
  //   "finished"     the transaction was released
  std::function<void(std::string_view stage)> observe;

  // Reports a libalpm error number (a non-zero `alpm_errno_t`) at a named stage, as if libalpm had failed there; zero
  // lets the run go on. Used to test the failure paths that cannot be provoked from outside. Consulted at "resolved"
  // (before the commit) and at "package-done" (after each package change; a fault there stops the remaining packages
  // and ends the run as a failure after a change).
  std::function<int(std::string_view stage)> inject_fault;

  // Replaces libalpm's downloader for the files of one execution: copy `url` into `destinationDirectory` and return 0,
  // return 1 when the file is up to date, or -1 on failure. A test can block here to hold a transaction in its download
  // phase. Unset in production (REQ-F-027, H-004).
  std::function<int(const std::string& url, const std::string& destinationDirectory, bool force)> fetch;
};

}  // namespace holonight_packages_transaction
