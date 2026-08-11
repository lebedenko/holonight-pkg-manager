#pragma once

#include "holonight_packages_domain/package.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace holonight_packages_domain {

enum class PackageSourceErrorCode : std::uint8_t { DatabaseRootInvalid, DatabaseOpenFailed, Unknown };

struct PackageSourceError {
  PackageSourceErrorCode code;
  std::string message;
};

class PackageSource {
 public:
  PackageSource() = default;
  PackageSource(const PackageSource&) = default;
  PackageSource(PackageSource&&) = default;
  PackageSource& operator=(const PackageSource&) = default;
  PackageSource& operator=(PackageSource&&) = default;
  virtual ~PackageSource();

  [[nodiscard]] virtual std::expected<std::vector<Package>, PackageSourceError> enumerateInstalledPackages() const = 0;
};

}  // namespace holonight_packages_domain
