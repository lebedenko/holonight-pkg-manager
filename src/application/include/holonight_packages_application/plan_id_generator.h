#pragma once

#include <string>

namespace holonight_packages_application {

using PlanId = std::string;

// Produces plan IDs: 32 lowercase hexadecimal characters (REQ-F-001).
class PlanIdGenerator {
 public:
  PlanIdGenerator() = default;
  PlanIdGenerator(const PlanIdGenerator&) = delete;
  PlanIdGenerator& operator=(const PlanIdGenerator&) = delete;
  PlanIdGenerator(PlanIdGenerator&&) = delete;
  PlanIdGenerator& operator=(PlanIdGenerator&&) = delete;
  virtual ~PlanIdGenerator();

  [[nodiscard]] virtual PlanId next() = 0;
};

// 128 random bits from the kernel CSPRNG (QRandomGenerator::system()).
class SystemPlanIdGenerator final : public PlanIdGenerator {
 public:
  [[nodiscard]] PlanId next() override;
};

}  // namespace holonight_packages_application
