#pragma once

#include "alpm_session.h"
#include "download_classifier.h"
#include "error_mapping.h"
#include "holonight_packages_domain/transaction_result.h"

#include <alpm.h>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_transaction {

// Where a commit is: decides how a libalpm failure is classified (DESIGN 10.2 of package-helper-transactions).
enum class CommitStage : unsigned char { BeforeCommitPoint, AtCommitPoint, AfterChange };

[[nodiscard]] bool isDownloadError(alpm_errno_t error) noexcept;
[[nodiscard]] bool logMentions(const std::vector<std::string>& lines, std::string_view text);

// Turns a failed libalpm call into an ExecutionResult with a code and the package names, and nothing else. Shared by
// the install and remove driver and the upgrade driver (system-upgrade DESIGN 5.6).
class FailureReporter {
 public:
  // `planned_names` are the packages of the plan, used to map a downloaded file name back to its package.
  explicit FailureReporter(std::vector<std::string> planned_names);

  // The planned package a downloaded file belongs to (`<name>-<version>-<arch>.pkg.tar.*`), or the file name itself.
  [[nodiscard]] std::string packageNameFor(const std::string& file_name) const;
  [[nodiscard]] static FailureContext failureContext(CommitStage stage) noexcept;
  // The packages a failure names, from libalpm's data list or the failed download.
  [[nodiscard]] std::vector<std::string> failureNames(alpm_errno_t error, alpm_list_t* data,
                                                      const DownloadClassification& download,
                                                      FailureContext context) const;
  // `changed` is reported with TransactionFailed after a package change.
  [[nodiscard]] holonight_packages_domain::ExecutionResult failureResult(
      AlpmSession& session, alpm_errno_t error, alpm_list_t* data, CommitStage stage,
      const std::vector<holonight_packages_domain::ChangedPackage>& changed) const;

 private:
  std::vector<std::string> planned_names_;
};

}  // namespace holonight_packages_transaction
