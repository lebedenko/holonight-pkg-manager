#pragma once

#include "holonight_packages_application/plan_id_generator.h"

#include <cstdint>
#include <format>

namespace holonight_packages_testing {

// Sequential IDs of the real shape: 32 lowercase hexadecimal characters.
class FakePlanIdGenerator final : public holonight_packages_application::PlanIdGenerator {
 public:
  [[nodiscard]] holonight_packages_application::PlanId next() override { return std::format("{:032x}", ++counter_); }

 private:
  std::uint64_t counter_ = 0;
};

}  // namespace holonight_packages_testing
