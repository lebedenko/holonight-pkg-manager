#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace holonight_packages_domain {

enum class SourceType : std::uint8_t { Official, Foreign };
enum class InstallReason : std::uint8_t { Explicit, Dependency };

struct Package {
  std::string identity;
  std::string name;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::string installedVersion;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  SourceType sourceType = SourceType::Foreign;
  std::string repository;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  InstallReason installReason = InstallReason::Explicit;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::string backendSpecificId;

  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::uint64_t sizeBytes = 0;
  std::string description;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::chrono::system_clock::time_point installDate;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::vector<std::string> requiredBy;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::vector<std::string> optionalDependencies;
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::size_t configFileCount = 0;

  bool operator==(const Package&) const = default;
};

}  // namespace holonight_packages_domain
