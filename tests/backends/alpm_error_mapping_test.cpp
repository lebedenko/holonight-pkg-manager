#include "alpm_error_mapping.h"

#include <gtest/gtest.h>

namespace holonight_packages_backends {
namespace {

using holonight_packages_domain::UpdateCheckErrorCode;

TEST(AlpmErrorMapping, LockContentionIsBusyForBothServerKinds) {
  EXPECT_EQ(classifyAlpmError(ALPM_ERR_HANDLE_LOCK, ServerKinds::AllLocal), UpdateCheckErrorCode::Busy);
  EXPECT_EQ(classifyAlpmError(ALPM_ERR_HANDLE_LOCK, ServerKinds::SomeRemote), UpdateCheckErrorCode::Busy);
}

TEST(AlpmErrorMapping, TransferFailuresDependOnWhetherAnyServerIsRemote) {
  for (const alpm_errno_t error :
       {ALPM_ERR_RETRIEVE, ALPM_ERR_RETRIEVE_PREPARE, ALPM_ERR_LIBCURL, ALPM_ERR_EXTERNAL_DOWNLOAD}) {
    EXPECT_EQ(classifyAlpmError(error, ServerKinds::SomeRemote), UpdateCheckErrorCode::NetworkUnavailable) << error;
    EXPECT_EQ(classifyAlpmError(error, ServerKinds::AllLocal), UpdateCheckErrorCode::RepositoryUnreachable) << error;
  }
}

TEST(AlpmErrorMapping, UnusableRepositoryAnswersAreRepositoryUnreachable) {
  for (const alpm_errno_t error : {
           ALPM_ERR_SERVER_BAD_URL,
           ALPM_ERR_SERVER_NONE,
           ALPM_ERR_DB_INVALID,
           ALPM_ERR_DB_VERSION,
           ALPM_ERR_DB_NOT_FOUND,
       }) {
    EXPECT_EQ(classifyAlpmError(error, ServerKinds::SomeRemote), UpdateCheckErrorCode::RepositoryUnreachable) << error;
    EXPECT_EQ(classifyAlpmError(error, ServerKinds::AllLocal), UpdateCheckErrorCode::RepositoryUnreachable) << error;
  }
}

TEST(AlpmErrorMapping, SignatureAndKeyringFailuresFailClosedAsUnknown) {
  for (const alpm_errno_t error :
       {ALPM_ERR_DB_INVALID_SIG, ALPM_ERR_SIG_MISSING, ALPM_ERR_SIG_INVALID, ALPM_ERR_GPGME}) {
    EXPECT_EQ(classifyAlpmError(error, ServerKinds::SomeRemote), UpdateCheckErrorCode::Unknown) << error;
    EXPECT_EQ(classifyAlpmError(error, ServerKinds::AllLocal), UpdateCheckErrorCode::Unknown) << error;
  }
}

TEST(AlpmErrorMapping, UnlistedErrorsAreUnknown) {
  for (const alpm_errno_t error : {
           ALPM_ERR_MEMORY,
           ALPM_ERR_SYSTEM,
           ALPM_ERR_BADPERMS,
           ALPM_ERR_DISK_SPACE,
           ALPM_ERR_NOT_A_DIR,
           ALPM_ERR_DB_OPEN,
           ALPM_ERR_DB_WRITE,
           ALPM_ERR_OK,
           ALPM_ERR_MISSING_CAPABILITY_SIGNATURES,
       }) {
    EXPECT_EQ(classifyAlpmError(error, ServerKinds::SomeRemote), UpdateCheckErrorCode::Unknown) << error;
  }
}

}  // namespace
}  // namespace holonight_packages_backends
