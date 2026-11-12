#pragma once

#include "holonight_packages_application/caller_identity.h"
#include "holonight_packages_application/plan_id_generator.h"
#include "holonight_packages_application/update_check_ports.h"
#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_domain/transaction_request.h"
#include "holonight_packages_domain/upgrade_plan.h"

#include <chrono>
#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace holonight_packages_application {

// Where a stored plan is in its life. Only Pending plans expire and can be discarded by a disconnect; Authorizing and
// Running plans belong to an Execute that is under way.
enum class PlanState : std::uint8_t { Pending, Authorizing, Running };

struct StoreLimits {
  std::size_t per_uid = 16;  // REQ-F-071
  std::size_t total = 64;
};

struct StoredPlan {
  PlanId id;
  CallerIdentity owner;
  holonight_packages_domain::PlanRequest request;
  holonight_packages_domain::ResolvedPlan plan;
  std::chrono::system_clock::time_point created;
  PlanState state = PlanState::Pending;
  // An upgrade plan's private scratch root and guard (system-upgrade); empty for install and remove.
  std::optional<holonight_packages_domain::UpgradeWorkspace> workspace;
};

// What find() found. Unknown plans, plans of another caller and plans that were consumed all read as NotFound, so a
// caller cannot probe for other users' plan IDs (REQ-F-004, REQ-F-007).
struct PlanLookup {
  enum class Kind : std::uint8_t { Found, NotFound, Expired };
  Kind kind = Kind::NotFound;
  const StoredPlan* plan = nullptr;
};

// Plans live only here, in memory (REQ-F-003, REQ-F-069). Not thread-safe: the service owns it on one thread.
class PlanStore {
 public:
  PlanStore(const Clock& clock, PlanIdGenerator& generator, StoreLimits limits = {},
            std::chrono::seconds lifetime = std::chrono::seconds(300));

  // False when a limit is reached; no plan is created then (REQ-F-071). Live means present and not expired.
  [[nodiscard]] bool hasRoomFor(std::uint32_t uid) const;
  // Stores a plan and returns its ID. Check hasRoomFor() first; add() itself refuses beyond the limits.
  [[nodiscard]] std::optional<PlanId> add(const CallerIdentity& owner, holonight_packages_domain::PlanRequest request,
                                          holonight_packages_domain::ResolvedPlan plan,
                                          std::optional<holonight_packages_domain::UpgradeWorkspace> workspace = {});

  // Called with every plan that leaves the store, whatever the reason: consumed, expired, swept or discarded by a
  // disconnect. The one place where an upgrade plan's scratch root is removed (system-upgrade DESIGN 4.3).
  void setDiscardObserver(std::function<void(const StoredPlan&)> observer) { observer_ = std::move(observer); }

  // A pending plan older than the lifetime is Expired and is discarded by this call. Exactly the lifetime is still
  // valid (REQ-F-005, REQ-F-006).
  [[nodiscard]] PlanLookup find(const PlanId& plan_id, const CallerIdentity& caller);

  void setState(const PlanId& plan_id, PlanState state);
  void consume(const PlanId& plan_id);  // removes the plan (REQ-F-008, REQ-F-010)
  void discard(const PlanId& plan_id) { consume(plan_id); }

  // A bound caller disconnected: removes its Pending plans (REQ-F-012). Returns how many.
  std::size_t discardPendingFor(std::string_view unique_name);
  // Removes expired Pending plans. Returns how many.
  std::size_t sweepExpired();
  // When the next Pending plan expires, if any.
  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> nextExpiry() const;

  [[nodiscard]] std::size_t size() const { return plans_.size(); }
  // Any plan at all keeps the helper alive (REQ-F-068).
  [[nodiscard]] bool hasActivity() const { return !plans_.empty(); }
  [[nodiscard]] const StoredPlan* peek(const PlanId& plan_id) const;

 private:
  [[nodiscard]] bool expired(const StoredPlan& plan) const;

  const Clock* clock_;
  PlanIdGenerator* generator_;
  StoreLimits limits_;
  std::chrono::seconds lifetime_;
  void notifyDiscarded(const StoredPlan& plan) const;

  std::map<PlanId, StoredPlan> plans_;
  std::function<void(const StoredPlan&)> observer_;
};

}  // namespace holonight_packages_application
