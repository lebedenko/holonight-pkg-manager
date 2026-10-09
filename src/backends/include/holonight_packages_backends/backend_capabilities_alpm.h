#pragma once

#include "holonight_packages_domain/backend_capabilities.h"

namespace holonight_packages_backends {

// The libalpm backend can refresh a private copy of the sync databases and compare it online.
[[nodiscard]] inline holonight_packages_domain::BackendCapabilities alpmBackendCapabilities() {
  return {.canCheckForUpdates = true};
}

}  // namespace holonight_packages_backends
