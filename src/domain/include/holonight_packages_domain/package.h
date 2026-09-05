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
  std::string installedVersion;
  SourceType sourceType = SourceType::Foreign;
  std::string repository;
  InstallReason installReason = InstallReason::Explicit;
  std::string backendSpecificId;

  std::uint64_t sizeBytes = 0;
  std::string description;
  std::chrono::system_clock::time_point installDate;
  std::vector<std::string> requiredBy;
  std::vector<std::string> optionalDependencies;
  std::size_t configFileCount = 0;

  bool operator==(const Package&) const = default;
};

}  // namespace holonight_packages_domain
