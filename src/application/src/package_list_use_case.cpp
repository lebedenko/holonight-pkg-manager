#include "holonight_packages_application/package_list_use_case.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace holonight_packages_application {

PackageListUseCase::PackageListUseCase(std::shared_ptr<holonight_packages_domain::PackageSource> source)
    : source_(std::move(source)) {
  if (!source_) {
    throw std::invalid_argument("PackageListUseCase requires a package source");
  }
}

std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>
PackageListUseCase::getInstalledPackages() const {
  auto result = source_->enumerateInstalledPackages();
  if (!result.has_value()) {
    return result;
  }
  std::ranges::sort(*result, {}, &holonight_packages_domain::Package::name);
  return result;
}

}  // namespace holonight_packages_application
