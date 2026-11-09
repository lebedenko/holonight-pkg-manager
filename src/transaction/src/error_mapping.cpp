#include "error_mapping.h"

#include <optional>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::TransactionErrorCode;
using Code = TransactionErrorCode;

bool isBeforeAnyChange(FailureContext context) {
  return context == FailureContext::Resolve || context == FailureContext::BeforeCommitPoint;
}

// Errors found while a database is being read.
std::optional<Code> mapDatabaseError(alpm_errno_t error, FailureContext context) {
  switch (error) {
    case ALPM_ERR_DB_OPEN:
    case ALPM_ERR_DB_NOT_FOUND:
    case ALPM_ERR_DB_INVALID:
    case ALPM_ERR_DB_INVALID_SIG:
    case ALPM_ERR_DB_VERSION:
    case ALPM_ERR_DB_NOT_NULL:
    case ALPM_ERR_DB_NULL:
    case ALPM_ERR_DB_WRITE:
    case ALPM_ERR_DB_REMOVE:
      return isBeforeAnyChange(context) ? Code::DatabaseUnavailable : Code::TransactionFailed;
    default:
      return std::nullopt;
  }
}

// Signature and key errors. A signature failure while a database is read is a database failure.
std::optional<Code> mapSignatureError(alpm_errno_t error, FailureContext context) {
  switch (error) {
    case ALPM_ERR_PKG_INVALID_SIG:
    case ALPM_ERR_PKG_MISSING_SIG:
    case ALPM_ERR_GPGME:
      return context == FailureContext::BeforeCommitPoint ? Code::SignatureInvalid : Code::TransactionFailed;
    case ALPM_ERR_SIG_MISSING:
    case ALPM_ERR_SIG_INVALID:
      if (context == FailureContext::Resolve) {
        return Code::DatabaseUnavailable;
      }
      return context == FailureContext::BeforeCommitPoint ? Code::SignatureInvalid : Code::TransactionFailed;
    default:
      return std::nullopt;
  }
}

// What libalpm checks inside the commit, before it changes anything.
std::optional<Code> mapCommitCheckError(alpm_errno_t error, FailureContext context, Code downloadCode) {
  const bool before = context == FailureContext::BeforeCommitPoint;
  switch (error) {
    case ALPM_ERR_RETRIEVE:
    case ALPM_ERR_LIBCURL:
    case ALPM_ERR_EXTERNAL_DOWNLOAD:
      return before ? downloadCode : Code::TransactionFailed;
    case ALPM_ERR_FILE_CONFLICTS:
      return before ? Code::FileConflicts : Code::TransactionFailed;
    case ALPM_ERR_DISK_SPACE:
      return before ? Code::DiskSpace : Code::TransactionFailed;
    case ALPM_ERR_PKG_INVALID:
    case ALPM_ERR_PKG_INVALID_CHECKSUM:
    case ALPM_ERR_PKG_OPEN:
    case ALPM_ERR_TRANS_HOOK_FAILED:
      return Code::TransactionFailed;
    default:
      return std::nullopt;
  }
}

// Resolution outcomes that name a package or a decision.
std::optional<Code> mapResolutionError(alpm_errno_t error, FailureContext context) {
  const bool resolving = context == FailureContext::Resolve;
  switch (error) {
    case ALPM_ERR_CONFLICTING_DEPS:
      return isBeforeAnyChange(context) ? Code::NeedsDecision : Code::TransactionFailed;
    case ALPM_ERR_PKG_NOT_FOUND:
      return resolving ? Code::PackageNotFound : Code::TransactionFailed;
    case ALPM_ERR_PKG_IGNORED:
      return resolving ? Code::PackageIgnored : Code::TransactionFailed;
    default:
      return std::nullopt;
  }
}

}  // namespace

TransactionErrorCode mapAlpmError(alpm_errno_t error, FailureContext context,
                                  TransactionErrorCode downloadCode) noexcept {
  if (error == ALPM_ERR_HANDLE_LOCK) {
    return Code::Busy;
  }
  for (const auto& mapped : {
           mapResolutionError(error, context),
           mapDatabaseError(error, context),
           mapSignatureError(error, context),
           mapCommitCheckError(error, context, downloadCode),
       }) {
    if (mapped) {
      return *mapped;
    }
  }
  // Unlisted errors, memory and system errors: a rollback before the first change, a failure after it.
  return isBeforeAnyChange(context) ? Code::InternalError : Code::TransactionFailed;
}

}  // namespace holonight_packages_transaction
