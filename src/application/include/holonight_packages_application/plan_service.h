#pragma once

#include "holonight_packages_application/authorizer.h"
#include "holonight_packages_application/caller_identity.h"
#include "holonight_packages_application/plan_id_generator.h"
#include "holonight_packages_application/plan_store.h"
#include "holonight_packages_application/terminator.h"
#include "holonight_packages_application/update_check_ports.h"
#include "holonight_packages_application/work_runner.h"
#include "holonight_packages_domain/transaction_engine.h"

#include <chrono>
#include <expected>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_application {

// The authorization action IDs of REQ-F-063 and REQ-F-064. apps/helper/HelperIdentity.h holds the same strings; the
// artifact check keeps them in agreement.
struct AuthorizationActions {
  std::string install = "org.holonight.packages.install";
  std::string remove = "org.holonight.packages.remove";
};

// Orchestrates the plan life cycle (DESIGN 4): validation, preparation, authorization, re-resolution, execution,
// cancellation (a cancel before the commit point replies and then ends the process through the Terminator). All entry
// points run on the service thread; only the engine's execute() runs on a worker thread, through the WorkRunner. One
// work slot: while a plan is being prepared, authorized or executed, PreparePlan and Execute answer Busy (REQ-F-031).
class PlanService {
 public:
  struct Dependencies {
    std::reference_wrapper<holonight_packages_domain::TransactionEngine> engine;
    std::reference_wrapper<Authorizer> authorizer;
    std::reference_wrapper<CallerIdentityResolver> identities;
    std::reference_wrapper<PlanIdGenerator> ids;
    std::reference_wrapper<const Clock> clock;
    std::reference_wrapper<WorkRunner> runner;
    std::reference_wrapper<Terminator> terminator;
  };

  struct Prepared {
    PlanId id;
    holonight_packages_domain::ResolvedPlan plan;
    std::chrono::system_clock::time_point expires_at;
  };
  using PrepareReply = std::function<void(std::expected<Prepared, holonight_packages_domain::TransactionFailure>)>;
  using ExecuteReply = std::function<void(holonight_packages_domain::ExecutionResult)>;
  // Called for every progress update, on the worker thread, with the plan owner's unique bus name.
  using ProgressForward = std::function<void(const std::string& ownerUniqueName, const PlanId&,
                                             const holonight_packages_domain::ProgressUpdate&)>;

  explicit PlanService(Dependencies dependencies, StoreLimits limits = {},
                       std::chrono::seconds lifetime = std::chrono::seconds(300), AuthorizationActions actions = {});

  // REQ-F-001: validates, then resolves without authorization. `reply` is called exactly once.
  void prepare(const std::string& unique_name, std::string_view action, const std::vector<std::string>& names,
               const std::vector<std::string>& optionKeys, PrepareReply reply);

  // REQ-F-044..048, REQ-F-065..066. `reply` is called exactly once.
  void execute(const std::string& unique_name, const PlanId& plan_id, ExecuteReply reply);

  // REQ-F-011, REQ-F-056..058: ok, or PlanNotFound, or PastPointOfNoReturn.
  [[nodiscard]] std::expected<void, holonight_packages_domain::TransactionFailure> cancel(
      const std::string& unique_name, const PlanId& plan_id);

  // SIGTERM/SIGINT/stop: cancels what can still be cancelled (authorization, or a run before the commit point) and
  // reports whether the helper may exit right away. After the commit point it returns false and the caller exits once
  // slotBusy() turns false, so a package change is never cut short.
  [[nodiscard]] bool shutdownRequested();

  // The caller's bus connection went away (REQ-F-012, REQ-F-059).
  void callerGone(const std::string& unique_name);

  void setProgressForward(ProgressForward forward) { progressForward_ = std::move(forward); }
  // Called whenever the plan store or the work slot changes, so an idle timer can re-evaluate.
  void setActivityObserver(std::function<void()> observer) { activityObserver_ = std::move(observer); }

  [[nodiscard]] bool slotBusy() const noexcept { return slot_ != Slot::Idle; }
  [[nodiscard]] bool hasActivity() const { return slot_ != Slot::Idle || store_.hasActivity(); }
  [[nodiscard]] std::size_t planCount() const { return store_.size(); }
  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> nextExpiry() const { return store_.nextExpiry(); }
  // Removes expired pending plans (called from a timer).
  void sweepExpired();

 private:
  enum class Slot : std::uint8_t { Idle, Preparing, Authorizing, Running };

  // The execution that holds the work slot.
  struct ActiveExecution {
    PlanId id;
    CallerIdentity owner;
    holonight_packages_domain::PlanRequest request;
    holonight_packages_domain::ResolvedPlan plan;
    ExecuteReply reply;
    // The Cancelled reply was already sent; the worker's late result is ignored (REQ-F-056).
    bool abandoned = false;
  };

  [[nodiscard]] std::optional<CallerIdentity> identify(const std::string& unique_name);
  void notifyActivity();
  void authorizationDecided(AuthorizationDecision decision);
  void startRunning();
  void finishRunning(holonight_packages_domain::ExecutionResult result);
  void finishCancelledDuringAuthorization();
  void abandonRunning();
  [[nodiscard]] std::expected<void, holonight_packages_domain::TransactionFailure> cancelActive();

  holonight_packages_domain::TransactionEngine* engine_;
  Authorizer* authorizer_;
  CallerIdentityResolver* identities_;
  const Clock* clock_;
  WorkRunner* runner_;
  Terminator* terminator_;
  PlanStore store_;
  AuthorizationActions actions_;
  Slot slot_ = Slot::Idle;
  std::optional<ActiveExecution> active_;
  // Shared with the worker thread: the engine's control handle and a cancel that arrived before it was published.
  std::mutex controlMutex_;
  holonight_packages_domain::ExecutionControl* control_ = nullptr;
  bool cancelRequested_ = false;
  bool controlRetired_ = false;
  ProgressForward progressForward_;
  std::function<void()> activityObserver_;
  std::chrono::seconds lifetime_;
};

}  // namespace holonight_packages_application
