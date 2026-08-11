#pragma once

#include <cstdint>
#include <string>

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

  bool operator==(const Package&) const = default;
};

}  // namespace holonight_packages_domain
