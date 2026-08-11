#pragma once

#include "holonight_packages_domain/package_source.h"

#include <gmock/gmock.h>

namespace holonight_packages_application {

class MockPackageSource : public holonight_packages_domain::PackageSource {
 public:
  using Result =
      std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>;

  MOCK_METHOD(
      (std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>),
      enumerateInstalledPackages, (), (const, override));
};

}  // namespace holonight_packages_application
