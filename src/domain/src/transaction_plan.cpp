#include "holonight_packages_domain/transaction_plan.h"

#include <algorithm>
#include <tuple>

namespace holonight_packages_domain {
namespace {

using InstallKey = std::tuple<std::string, std::string, std::string>;
using RemovalKey = std::tuple<std::string, std::string, ChangeKind>;

std::vector<InstallKey> installKeys(const ResolvedPlan& plan) {
  std::vector<InstallKey> keys;
  for (const PlannedChange& change : plan.changes) {
    if (change.kind != ChangeKind::Orphan && !plan.isRemoval()) {
      keys.emplace_back(change.name, change.new_version, change.repository);
    }
  }
  std::ranges::sort(keys);
  return keys;
}

std::vector<RemovalKey> removalKeys(const ResolvedPlan& plan) {
  std::vector<RemovalKey> keys;
  for (const PlannedChange& change : plan.changes) {
    if (plan.isRemoval() || change.kind == ChangeKind::Orphan) {
      keys.emplace_back(change.name, change.old_version, change.kind);
    }
  }
  std::ranges::sort(keys);
  return keys;
}

}  // namespace

std::string_view changeKindName(ChangeKind kind) noexcept {
  switch (kind) {
    case ChangeKind::Target:
      return "target";
    case ChangeKind::Dependency:
      return "dependency";
    case ChangeKind::Orphan:
      return "orphan";
  }
  return "target";
}

bool samePlan(const ResolvedPlan& left, const ResolvedPlan& right) {
  return left.action == right.action && installKeys(left) == installKeys(right) &&
         removalKeys(left) == removalKeys(right) && left.hooks == right.hooks;
}

}  // namespace holonight_packages_domain
