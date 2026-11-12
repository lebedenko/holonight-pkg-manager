#include "failure_reporter.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

std::string fileNameOf(const char* path) {
  return path == nullptr ? std::string() : std::filesystem::path(path).filename().string();
}

}  // namespace

bool isDownloadError(alpm_errno_t error) noexcept {
  return error == ALPM_ERR_RETRIEVE || error == ALPM_ERR_LIBCURL || error == ALPM_ERR_EXTERNAL_DOWNLOAD;
}

bool logMentions(const std::vector<std::string>& lines, std::string_view text) {
  return std::ranges::any_of(lines, [text](const std::string& line) { return line.contains(text); });
}

FailureReporter::FailureReporter(std::vector<std::string> planned_names) : planned_names_(std::move(planned_names)) {}

std::string FailureReporter::packageNameFor(const std::string& file_name) const {
  for (const auto& name : planned_names_) {
    if (file_name.starts_with(name + "-")) {
      return name;
    }
  }
  return file_name;
}

FailureContext FailureReporter::failureContext(CommitStage stage) noexcept {
  switch (stage) {
    case CommitStage::BeforeCommitPoint:
      return FailureContext::BeforeCommitPoint;
    case CommitStage::AtCommitPoint:
      return FailureContext::AtCommitPointBeforeChange;
    case CommitStage::AfterChange:
      break;
  }
  return FailureContext::AfterChange;
}

std::vector<std::string> FailureReporter::failureNames(alpm_errno_t error, alpm_list_t* data,
                                                       const DownloadClassification& download,
                                                       FailureContext context) const {
  std::vector<std::string> names;
  if (isDownloadError(error)) {
    if (!download.file_name.empty() && context == FailureContext::BeforeCommitPoint) {
      names.push_back(packageNameFor(download.file_name));
    }
    return names;
  }
  for (alpm_list_t* item = data; item != nullptr; item = item->next) {
    if (error == ALPM_ERR_FILE_CONFLICTS) {
      const auto* conflict = static_cast<const alpm_fileconflict_t*>(item->data);
      if (conflict->target != nullptr) {
        names.emplace_back(conflict->target);
      }
    } else if (error == ALPM_ERR_PKG_INVALID || error == ALPM_ERR_PKG_INVALID_CHECKSUM ||
               error == ALPM_ERR_PKG_INVALID_SIG || error == ALPM_ERR_PKG_MISSING_SIG) {
      names.push_back(fileNameOf(static_cast<const char*>(item->data)));
    }
  }
  return names;
}

ExecutionResult FailureReporter::failureResult(
    AlpmSession& session, alpm_errno_t error, alpm_list_t* data, CommitStage stage,
    const std::vector<holonight_packages_domain::ChangedPackage>& changed) const {
  const FailureContext context = failureContext(stage);
  DownloadClassification download;
  if (isDownloadError(error)) {
    download = classifyDownloadFailure(session.logLines(), &systemStatError);
  }
  TransactionErrorCode code = mapAlpmError(error, context, download.code);
  // libalpm reports a missing signing key without an errno (spike S-6).
  const bool unmapped = code == TransactionErrorCode::InternalError || code == TransactionErrorCode::TransactionFailed;
  if (unmapped && logMentions(session.logLines(), "required key missing from keyring")) {
    code = TransactionErrorCode::SignatureInvalid;
  }
  TransactionFailure failure{.code = code, .names = {}, .choices = {}};
  if (stage != CommitStage::AfterChange && !session.questions().decisions().empty()) {
    failure.code = TransactionErrorCode::NeedsDecision;
    failure.choices = session.questions().decisions();
    return ExecutionResult::failed(std::move(failure));
  }
  failure.names = failureNames(error, data, download, context);
  ExecutionResult result = ExecutionResult::failed(std::move(failure));
  if (code == TransactionErrorCode::TransactionFailed && stage == CommitStage::AfterChange) {
    result.changed = changed;
  }
  return result;
}

}  // namespace holonight_packages_transaction
