#pragma once

#include "holonight_packages_domain/transaction_request.h"

#include <cstdint>
#include <string>
#include <vector>

namespace holonight_packages_domain {

// Why a package is part of the plan. Targets were named by the caller; the others were derived.
enum class ChangeKind : std::uint8_t { Target, Dependency, Orphan };

[[nodiscard]] std::string_view changeKindName(ChangeKind kind) noexcept;

struct PlannedChange {
  std::string name;
  ChangeKind kind = ChangeKind::Target;
  std::string old_version;           // installed version; empty on a fresh install
  std::string new_version;           // empty on removal
  std::string repository;            // sync repository; empty on removal (remove plans open no sync database)
  std::uint64_t download_bytes = 0;  // volatile: not part of plan equality

  bool operator==(const PlannedChange&) const = default;
};

struct HookPreview {
  std::string name;
  bool conditional = false;  // a file trigger could not be evaluated before the download

  bool operator==(const HookPreview&) const = default;
};

// The resolved plan that a preview shows and that Execute re-resolves and compares.
struct ResolvedPlan {
  PlanAction action = PlanAction::Install;
  std::vector<PlannedChange> changes;     // sorted by (kind, name)
  std::vector<HookPreview> hooks;         // libalpm hook order
  std::vector<std::string> mirror_hosts;  // volatile: journal only

  [[nodiscard]] bool isRemoval() const noexcept { return action == PlanAction::Remove; }
};

// REQ-F-043: equal action; equal install sets by (name, new_version, repository), role-insensitive; equal removal sets
// by (name, old_version) with the Target/Orphan role kept; equal ordered hook lists. Download sizes and mirror hosts
// never take part.
[[nodiscard]] bool samePlan(const ResolvedPlan& left, const ResolvedPlan& right);

}  // namespace holonight_packages_domain
