#include "holonight_packages_application/plan_store.h"

#include <algorithm>
#include <vector>

namespace holonight_packages_application {

PlanStore::PlanStore(const Clock& clock, PlanIdGenerator& generator, StoreLimits limits, std::chrono::seconds lifetime)
    : clock_(&clock), generator_(&generator), limits_(limits), lifetime_(lifetime) {}

bool PlanStore::expired(const StoredPlan& plan) const {
  return plan.state == PlanState::Pending && clock_->now() - plan.created > lifetime_;
}

bool PlanStore::hasRoomFor(std::uint32_t uid) const {
  std::size_t live = 0;
  std::size_t forUid = 0;
  for (const auto& [plan_id, plan] : plans_) {
    if (expired(plan)) {
      continue;
    }
    ++live;
    forUid += plan.owner.uid == uid ? 1U : 0U;
  }
  return live < limits_.total && forUid < limits_.per_uid;
}

void PlanStore::notifyDiscarded(const StoredPlan& plan) const {
  if (observer_) {
    observer_(plan);
  }
}

std::optional<PlanId> PlanStore::add(const CallerIdentity& owner, holonight_packages_domain::PlanRequest request,
                                     holonight_packages_domain::ResolvedPlan plan,
                                     std::optional<holonight_packages_domain::UpgradeWorkspace> workspace) {
  if (!hasRoomFor(owner.uid)) {
    return std::nullopt;
  }
  PlanId plan_id = generator_->next();
  while (plans_.contains(plan_id)) {
    plan_id = generator_->next();
  }
  plans_.emplace(plan_id, StoredPlan{
                              .id = plan_id,
                              .owner = owner,
                              .request = std::move(request),
                              .plan = std::move(plan),
                              .created = clock_->now(),
                              .state = PlanState::Pending,
                              .workspace = std::move(workspace),
                          });
  return plan_id;
}

PlanLookup PlanStore::find(const PlanId& plan_id, const CallerIdentity& caller) {
  const auto found = plans_.find(plan_id);
  if (found == plans_.end() || found->second.owner != caller) {
    return PlanLookup{.kind = PlanLookup::Kind::NotFound, .plan = nullptr};
  }
  if (expired(found->second)) {
    const StoredPlan gone = std::move(found->second);
    plans_.erase(found);
    notifyDiscarded(gone);
    return PlanLookup{.kind = PlanLookup::Kind::Expired, .plan = nullptr};
  }
  return PlanLookup{.kind = PlanLookup::Kind::Found, .plan = &found->second};
}

const StoredPlan* PlanStore::peek(const PlanId& plan_id) const {
  const auto found = plans_.find(plan_id);
  return found == plans_.end() ? nullptr : &found->second;
}

void PlanStore::setState(const PlanId& plan_id, PlanState state) {
  if (const auto found = plans_.find(plan_id); found != plans_.end()) {
    found->second.state = state;
  }
}

void PlanStore::consume(const PlanId& plan_id) {
  if (const auto found = plans_.find(plan_id); found != plans_.end()) {
    const StoredPlan gone = std::move(found->second);
    plans_.erase(found);
    notifyDiscarded(gone);
  }
}

std::size_t PlanStore::discardPendingFor(std::string_view unique_name) {
  std::vector<StoredPlan> gone;
  std::erase_if(plans_, [unique_name, &gone](auto& entry) {
    const bool discard = entry.second.state == PlanState::Pending && entry.second.owner.unique_name == unique_name;
    if (discard) {
      gone.push_back(std::move(entry.second));
    }
    return discard;
  });
  for (const StoredPlan& plan : gone) {
    notifyDiscarded(plan);
  }
  return gone.size();
}

std::size_t PlanStore::sweepExpired() {
  std::vector<StoredPlan> gone;
  std::erase_if(plans_, [this, &gone](auto& entry) {
    const bool discard = expired(entry.second);
    if (discard) {
      gone.push_back(std::move(entry.second));
    }
    return discard;
  });
  for (const StoredPlan& plan : gone) {
    notifyDiscarded(plan);
  }
  return gone.size();
}

std::optional<std::chrono::system_clock::time_point> PlanStore::nextExpiry() const {
  std::optional<std::chrono::system_clock::time_point> earliest;
  for (const auto& [plan_id, plan] : plans_) {
    if (plan.state == PlanState::Pending) {
      const auto due = plan.created + lifetime_;
      earliest = earliest ? std::min(*earliest, due) : due;
    }
  }
  return earliest;
}

}  // namespace holonight_packages_application
