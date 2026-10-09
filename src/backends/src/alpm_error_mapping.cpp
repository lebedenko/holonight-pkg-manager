#include "alpm_error_mapping.h"

namespace holonight_packages_backends {

holonight_packages_domain::UpdateCheckErrorCode classifyAlpmError(alpm_errno_t error, ServerKinds kinds) noexcept {
  using holonight_packages_domain::UpdateCheckErrorCode;
  switch (error) {
    case ALPM_ERR_HANDLE_LOCK:
      return UpdateCheckErrorCode::Busy;
    case ALPM_ERR_RETRIEVE:
    case ALPM_ERR_RETRIEVE_PREPARE:
    case ALPM_ERR_LIBCURL:
    case ALPM_ERR_EXTERNAL_DOWNLOAD:
      return kinds == ServerKinds::SomeRemote ? UpdateCheckErrorCode::NetworkUnavailable
                                              : UpdateCheckErrorCode::RepositoryUnreachable;
    case ALPM_ERR_SERVER_BAD_URL:
    case ALPM_ERR_SERVER_NONE:
    case ALPM_ERR_DB_INVALID:
    case ALPM_ERR_DB_VERSION:
    case ALPM_ERR_DB_NOT_FOUND:
      return UpdateCheckErrorCode::RepositoryUnreachable;
    case ALPM_ERR_DB_INVALID_SIG:
    case ALPM_ERR_SIG_MISSING:
    case ALPM_ERR_SIG_INVALID:
    case ALPM_ERR_GPGME:
    default:
      return UpdateCheckErrorCode::Unknown;
  }
}

}  // namespace holonight_packages_backends
