#include "holonight_packages_application/authorizer.h"
#include "holonight_packages_application/caller_identity.h"
#include "holonight_packages_application/plan_id_generator.h"

#include <QRandomGenerator>

#include <array>
#include <cstdint>

namespace holonight_packages_application {

Authorizer::~Authorizer() = default;
PlanIdGenerator::~PlanIdGenerator() = default;
CallerIdentityResolver::~CallerIdentityResolver() = default;

PlanId SystemPlanIdGenerator::next() {
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::array<quint32, 4> words{};
  QRandomGenerator::system()->fillRange(words.data(), words.size());
  PlanId plan_id;
  plan_id.reserve(32);
  for (const quint32 word : words) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      plan_id.push_back(kHex[(word >> shift) & 0xFU]);
    }
  }
  return plan_id;
}

}  // namespace holonight_packages_application
