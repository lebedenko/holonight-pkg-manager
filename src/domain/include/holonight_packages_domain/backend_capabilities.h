#pragma once

namespace holonight_packages_domain {

struct BackendCapabilities {
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool canCheckForUpdates = false;

  bool operator==(const BackendCapabilities&) const = default;
};

}  // namespace holonight_packages_domain
