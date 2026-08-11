#pragma once

#include "holonight_packages_domain/package_source.h"

#include <filesystem>

namespace holonight_packages_backends {

class AlpmPackageSource : public holonight_packages_domain::PackageSource {
 public:
  AlpmPackageSource(std::filesystem::path database_root, std::filesystem::path database_path);

  [[nodiscard]] std::expected<std::vector<holonight_packages_domain::Package>,
                              holonight_packages_domain::PackageSourceError>
  enumerateInstalledPackages() const override;

 private:
  std::filesystem::path database_root_;
  std::filesystem::path database_path_;
};

}  // namespace holonight_packages_backends
