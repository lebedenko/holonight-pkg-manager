#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace holonight_packages_application {

// Who owns a plan: the unique bus name and the Unix user behind it (REQ-F-004).
struct CallerIdentity {
  std::string unique_name;
  std::uint32_t uid = 0;

  bool operator==(const CallerIdentity&) const = default;
};

// Looks up the Unix user of a bus connection. Production: GetConnectionUnixUser. A failed lookup is nullopt.
class CallerIdentityResolver {
 public:
  CallerIdentityResolver() = default;
  CallerIdentityResolver(const CallerIdentityResolver&) = delete;
  CallerIdentityResolver& operator=(const CallerIdentityResolver&) = delete;
  CallerIdentityResolver(CallerIdentityResolver&&) = delete;
  CallerIdentityResolver& operator=(CallerIdentityResolver&&) = delete;
  virtual ~CallerIdentityResolver();

  [[nodiscard]] virtual std::optional<std::uint32_t> uidOf(const std::string& unique_name) = 0;
};

}  // namespace holonight_packages_application
