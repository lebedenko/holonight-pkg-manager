#pragma once

#include "holonight_packages_domain/package.h"
#include "holonight_packages_domain/package_source.h"

#include <expected>
#include <memory>
#include <vector>

namespace holonight_packages_application {

class PackageListUseCase {
 public:
  explicit PackageListUseCase(std::shared_ptr<holonight_packages_domain::PackageSource> source);

  [[nodiscard]] std::expected<std::vector<holonight_packages_domain::Package>,
                              holonight_packages_domain::PackageSourceError>
  enumerateInstalledPackages() const;

 private:
  std::shared_ptr<holonight_packages_domain::PackageSource> source_;
};

}  // namespace holonight_packages_application
