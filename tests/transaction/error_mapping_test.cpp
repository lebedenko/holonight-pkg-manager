#include "error_mapping.h"

#include <gtest/gtest.h>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::TransactionErrorCode;
using Code = TransactionErrorCode;

Code map(alpm_errno_t error, FailureContext context, Code download = Code::DownloadFailed) {
  return mapAlpmError(error, context, download);
}

// One case per row of DESIGN 10.2.
TEST(ErrorMappingTest, ALockHeldElsewhereIsBusyInEveryContext) {
  for (const auto context : {
           FailureContext::Resolve,
           FailureContext::BeforeCommitPoint,
           FailureContext::AtCommitPointBeforeChange,
           FailureContext::AfterChange,
       }) {
    EXPECT_EQ(map(ALPM_ERR_HANDLE_LOCK, context), Code::Busy);
  }
}

TEST(ErrorMappingTest, ResolutionErrors) {
  EXPECT_EQ(map(ALPM_ERR_CONFLICTING_DEPS, FailureContext::Resolve), Code::NeedsDecision);
  EXPECT_EQ(map(ALPM_ERR_PKG_NOT_FOUND, FailureContext::Resolve), Code::PackageNotFound);
  EXPECT_EQ(map(ALPM_ERR_PKG_IGNORED, FailureContext::Resolve), Code::PackageIgnored);
  EXPECT_EQ(map(ALPM_ERR_MEMORY, FailureContext::Resolve), Code::InternalError);
  EXPECT_EQ(map(ALPM_ERR_SYSTEM, FailureContext::Resolve), Code::InternalError);
}

TEST(ErrorMappingTest, ASignatureFailureWhileADatabaseIsReadIsADatabaseFailure) {
  EXPECT_EQ(map(ALPM_ERR_SIG_MISSING, FailureContext::Resolve), Code::DatabaseUnavailable);
  EXPECT_EQ(map(ALPM_ERR_SIG_INVALID, FailureContext::Resolve), Code::DatabaseUnavailable);
  EXPECT_EQ(map(ALPM_ERR_SIG_MISSING, FailureContext::BeforeCommitPoint), Code::SignatureInvalid);
  EXPECT_EQ(map(ALPM_ERR_SIG_INVALID, FailureContext::BeforeCommitPoint), Code::SignatureInvalid);
}

TEST(ErrorMappingTest, DatabaseErrorsAreUnavailableBeforeAnyChange) {
  for (const alpm_errno_t error :
       {ALPM_ERR_DB_OPEN, ALPM_ERR_DB_NOT_FOUND, ALPM_ERR_DB_INVALID, ALPM_ERR_DB_INVALID_SIG, ALPM_ERR_DB_VERSION}) {
    EXPECT_EQ(map(error, FailureContext::Resolve), Code::DatabaseUnavailable) << error;
    EXPECT_EQ(map(error, FailureContext::BeforeCommitPoint), Code::DatabaseUnavailable) << error;
    EXPECT_EQ(map(error, FailureContext::AfterChange), Code::TransactionFailed) << error;
  }
}

TEST(ErrorMappingTest, DownloadErrorsTakeTheClassifiersVerdictBeforeTheCommitPointOnly) {
  for (const alpm_errno_t error : {ALPM_ERR_RETRIEVE, ALPM_ERR_LIBCURL, ALPM_ERR_EXTERNAL_DOWNLOAD}) {
    EXPECT_EQ(map(error, FailureContext::BeforeCommitPoint, Code::SyncDatabaseStale), Code::SyncDatabaseStale);
    EXPECT_EQ(map(error, FailureContext::BeforeCommitPoint, Code::DownloadFailed), Code::DownloadFailed);
    EXPECT_EQ(map(error, FailureContext::BeforeCommitPoint, Code::SignatureInvalid), Code::SignatureInvalid);
    EXPECT_EQ(map(error, FailureContext::AfterChange, Code::SyncDatabaseStale), Code::TransactionFailed);
  }
}

TEST(ErrorMappingTest, PreCommitCheckFailures) {
  EXPECT_EQ(map(ALPM_ERR_FILE_CONFLICTS, FailureContext::BeforeCommitPoint), Code::FileConflicts);
  EXPECT_EQ(map(ALPM_ERR_DISK_SPACE, FailureContext::BeforeCommitPoint), Code::DiskSpace);
  EXPECT_EQ(map(ALPM_ERR_FILE_CONFLICTS, FailureContext::AfterChange), Code::TransactionFailed);
  for (const alpm_errno_t error : {ALPM_ERR_PKG_INVALID_SIG, ALPM_ERR_PKG_MISSING_SIG, ALPM_ERR_GPGME}) {
    EXPECT_EQ(map(error, FailureContext::BeforeCommitPoint), Code::SignatureInvalid) << error;
  }
}

TEST(ErrorMappingTest, CorruptionAndHookFailuresAreTransactionFailedSoTheRetryCodeHolds) {
  for (const alpm_errno_t error :
       {ALPM_ERR_PKG_INVALID, ALPM_ERR_PKG_INVALID_CHECKSUM, ALPM_ERR_PKG_OPEN, ALPM_ERR_TRANS_HOOK_FAILED}) {
    EXPECT_EQ(map(error, FailureContext::BeforeCommitPoint), Code::TransactionFailed) << error;
    EXPECT_EQ(map(error, FailureContext::AtCommitPointBeforeChange), Code::TransactionFailed) << error;
  }
}

TEST(ErrorMappingTest, UnlistedErrorsRollBackBeforeTheCommitPointAndFailAfterIt) {
  for (const alpm_errno_t error : {ALPM_ERR_MEMORY, ALPM_ERR_SYSTEM, ALPM_ERR_RETRIEVE_PREPARE, ALPM_ERR_WRONG_ARGS}) {
    EXPECT_EQ(map(error, FailureContext::BeforeCommitPoint), Code::InternalError) << error;
    EXPECT_EQ(map(error, FailureContext::AtCommitPointBeforeChange), Code::TransactionFailed) << error;
    EXPECT_EQ(map(error, FailureContext::AfterChange), Code::TransactionFailed) << error;
  }
}

TEST(ErrorMappingTest, AnythingAfterAPackageChangeHasStartedIsTransactionFailed) {
  for (const alpm_errno_t error : {
           ALPM_ERR_PKG_INVALID_SIG,
           ALPM_ERR_DISK_SPACE,
           ALPM_ERR_CONFLICTING_DEPS,
           ALPM_ERR_PKG_NOT_FOUND,
           ALPM_ERR_MEMORY,
       }) {
    EXPECT_EQ(map(error, FailureContext::AfterChange), Code::TransactionFailed) << error;
  }
}

}  // namespace
}  // namespace holonight_packages_transaction
