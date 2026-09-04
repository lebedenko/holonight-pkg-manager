#pragma once

#include "holonight_packages_domain/package.h"
#include "holonight_packages_domain/package_source.h"

#include <expected>

namespace holonight_packages_backends::detail {

[[nodiscard]] std::expected<holonight_packages_domain::Package, holonight_packages_domain::PackageSourceError>
convertPackageFields(const char* name, const char* version, const char* repository,
                     holonight_packages_domain::SourceType source_type,
                     holonight_packages_domain::InstallReason install_reason);

}  // namespace holonight_packages_backends::detail
