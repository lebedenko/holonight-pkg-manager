#pragma once

#include "holonight_packages_domain/update_checker.h"

#include <alpm.h>
#include <cstdint>

namespace holonight_packages_backends {

// Whether every configured server of the repositories being refreshed is a file:// URL.
enum class ServerKinds : std::uint8_t { AllLocal, SomeRemote };

// The only libalpm-errno to UpdateCheckErrorCode table (DESIGN 5.3). Signature and keyring failures map to Unknown
// so the check fails closed; every unlisted value is Unknown.
[[nodiscard]] holonight_packages_domain::UpdateCheckErrorCode classifyAlpmError(alpm_errno_t error,
                                                                                ServerKinds kinds) noexcept;

}  // namespace holonight_packages_backends
