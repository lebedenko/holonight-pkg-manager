#include "holonight_packages_application/package_list_use_case.h"

#include "holonight_packages_domain/require_non_null.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace {

unsigned char foldedAscii(unsigned char value) {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<unsigned char>(value + ('a' - 'A'));
  }
  return value;
}

bool bytewiseLess(std::string_view lhs, std::string_view rhs) {
  const std::size_t common_size = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < common_size; ++index) {
    const auto lhs_byte = static_cast<unsigned char>(lhs[index]);
    const auto rhs_byte = static_cast<unsigned char>(rhs[index]);
    if (lhs_byte != rhs_byte) {
      return lhs_byte < rhs_byte;
    }
  }
  return lhs.size() < rhs.size();
}

bool packageNameLess(std::string_view lhs, std::string_view rhs) {
  const std::size_t common_size = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < common_size; ++index) {
    const unsigned char lhs_folded = foldedAscii(static_cast<unsigned char>(lhs[index]));
    const unsigned char rhs_folded = foldedAscii(static_cast<unsigned char>(rhs[index]));
    if (lhs_folded != rhs_folded) {
      return lhs_folded < rhs_folded;
    }
  }
  if (lhs.size() != rhs.size()) {
    return lhs.size() < rhs.size();
  }
  return bytewiseLess(lhs, rhs);
}

}  // namespace

namespace holonight_packages_application {

PackageListUseCase::PackageListUseCase(std::shared_ptr<holonight_packages_domain::PackageSource> source)
    : source_(holonight_packages_domain::requireNonNull(std::move(source),
                                                        "PackageListUseCase requires a package source")) {}

std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>
PackageListUseCase::enumerateInstalledPackages() const {
  auto result = source_->enumerateInstalledPackages();
  if (!result.has_value()) {
    return result;
  }
  std::ranges::sort(*result, packageNameLess, &holonight_packages_domain::Package::name);
  return result;
}

}  // namespace holonight_packages_application
