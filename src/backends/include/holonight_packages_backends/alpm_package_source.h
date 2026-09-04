#pragma once

#include "holonight_packages_domain/package_source.h"

#include <filesystem>
#include <memory>

namespace holonight_packages_persistence {
class AlpmConnectionCache;
}  // namespace holonight_packages_persistence

namespace holonight_packages_backends {

class AlpmPackageSource : public holonight_packages_domain::PackageSource {
 public:
  AlpmPackageSource(std::filesystem::path database_root, std::filesystem::path database_path);
  ~AlpmPackageSource() override;

  AlpmPackageSource(const AlpmPackageSource&) = delete;
  AlpmPackageSource& operator=(const AlpmPackageSource&) = delete;
  AlpmPackageSource(AlpmPackageSource&&) = delete;
  AlpmPackageSource& operator=(AlpmPackageSource&&) = delete;

  [[nodiscard]] std::expected<std::vector<holonight_packages_domain::Package>,
                              holonight_packages_domain::PackageSourceError>
  enumerateInstalledPackages() const override;

 private:
  std::filesystem::path database_root_;
  std::filesystem::path database_path_;
  std::unique_ptr<holonight_packages_persistence::AlpmConnectionCache> connection_cache_;
};

}  // namespace holonight_packages_backends
