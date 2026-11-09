#pragma once

#include "holonight_packages_application/caller_identity.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace holonight_packages_testing {

class FakeCallerIdentity final : public holonight_packages_application::CallerIdentityResolver {
 public:
  void set(const std::string& unique_name, std::uint32_t uid) { uids_[unique_name] = uid; }
  void forget(const std::string& unique_name) { uids_.erase(unique_name); }

  [[nodiscard]] std::optional<std::uint32_t> uidOf(const std::string& unique_name) override {
    const auto found = uids_.find(unique_name);
    return found == uids_.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
  }

 private:
  std::map<std::string, std::uint32_t> uids_;
};

}  // namespace holonight_packages_testing
