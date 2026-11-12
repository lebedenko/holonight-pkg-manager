#include "holonight_packages_application/plan_service.h"

#include <memory>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::CancelDecision;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionControl;
using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

TransactionFailure failure(TransactionErrorCode code) {
  return TransactionFailure{.code = code, .names = {}, .choices = {}};
}

std::string joinNames(const std::vector<std::string>& names) {
  std::string joined;
  for (const std::string& name : names) {
    if (!joined.empty()) {
      joined += ' ';
    }
    joined += name;
  }
  return joined;
}

}  // namespace

WorkRunner::~WorkRunner() = default;
Terminator::~Terminator() = default;

PlanService::PlanService(Dependencies dependencies, StoreLimits limits, std::chrono::seconds lifetime,
                         AuthorizationActions actions)
    : engine_(&dependencies.engine.get()),
      authorizer_(&dependencies.authorizer.get()),
      identities_(&dependencies.identities.get()),
      clock_(&dependencies.clock.get()),
      runner_(&dependencies.runner.get()),
      terminator_(&dependencies.terminator.get()),
      store_(dependencies.clock, dependencies.ids, limits, lifetime),
      actions_(std::move(actions)),
      scratch_(dependencies.scratch),
      lifetime_(lifetime) {
  // Every exit of an upgrade plan from the store removes its scratch root (system-upgrade REQ-F-013, REQ-F-014).
  store_.setDiscardObserver([this](const StoredPlan& plan) {
    if (plan.workspace && scratch_ != nullptr) {
      scratch_->remove(plan.workspace->scratch_root);
    }
  });
}

ScratchRoots::~ScratchRoots() = default;

void PlanService::notifyActivity() {
  if (activityObserver_) {
    activityObserver_();
  }
}

std::optional<CallerIdentity> PlanService::identify(const std::string& unique_name) {
  const auto uid = identities_->uidOf(unique_name);
  if (!uid) {
    return std::nullopt;
  }
  return CallerIdentity{.unique_name = unique_name, .uid = *uid};
}

void PlanService::sweepExpired() {
  store_.sweepExpired();
  notifyActivity();
}

void PlanService::prepare(const std::string& unique_name, std::string_view action,
                          const std::vector<std::string>& names, const std::vector<std::string>& optionKeys,
                          PrepareReply reply) {
  // Validation first: nothing below may run for an invalid request, so libalpm stays untouched (REQ-F-014).
  auto request = holonight_packages_domain::validateRequest(action, names, optionKeys);
  if (!request) {
    reply(std::unexpected(std::move(request.error())));
    return;
  }
  if (slot_ != Slot::Idle) {
    reply(std::unexpected(failure(TransactionErrorCode::Busy)));
    return;
  }
  const auto caller = identify(unique_name);
  if (!caller) {
    reply(std::unexpected(failure(TransactionErrorCode::InternalError)));
    return;
  }
  // Capacity is checked before the engine runs: a refused request costs nothing (REQ-F-071).
  if (!store_.hasRoomFor(caller->uid)) {
    reply(std::unexpected(failure(TransactionErrorCode::Busy)));
    return;
  }
  slot_ = Slot::Preparing;
  notifyActivity();
  struct Shared {
    holonight_packages_domain::PlanRequest request;
    std::optional<std::expected<ResolvedPlan, TransactionFailure>> resolved;
  };
  auto shared = std::make_shared<Shared>();
  shared->request = *request;
  runner_->run([this, shared] { shared->resolved = engine_->resolve(shared->request); },
               [this, shared, caller = *caller, reply = std::move(reply)] {
                 slot_ = Slot::Idle;
                 if (!shared->resolved->has_value()) {
                   notifyActivity();
                   reply(std::unexpected(std::move(shared->resolved->error())));
                   return;
                 }
                 const auto plan_id = store_.add(caller, shared->request, std::move(**shared->resolved));
                 notifyActivity();
                 if (!plan_id) {
                   reply(std::unexpected(failure(TransactionErrorCode::Busy)));
                   return;
                 }
                 const StoredPlan* stored = store_.peek(*plan_id);
                 reply(Prepared{.id = *plan_id, .plan = stored->plan, .expires_at = stored->created + lifetime_});
               });
}

void PlanService::prepareUpgrade(const std::string& unique_name, const std::vector<std::string>& optionKeys,
                                 PrepareReply reply) {
  auto request = holonight_packages_domain::validateUpgradeRequest(optionKeys);
  if (!request) {
    reply(std::unexpected(std::move(request.error())));
    return;
  }
  if (slot_ != Slot::Idle) {
    reply(std::unexpected(failure(TransactionErrorCode::Busy)));
    return;
  }
  const auto caller = identify(unique_name);
  if (!caller || scratch_ == nullptr) {
    reply(std::unexpected(failure(TransactionErrorCode::InternalError)));
    return;
  }
  // Capacity first: a refused request creates no scratch root (REQ-F-005).
  if (!store_.hasRoomFor(caller->uid)) {
    reply(std::unexpected(failure(TransactionErrorCode::Busy)));
    return;
  }
  auto root = scratch_->create();
  if (!root) {
    reply(std::unexpected(failure(TransactionErrorCode::InternalError)));
    return;
  }
  slot_ = Slot::Preparing;
  preparation_ =
      ActivePreparation{.owner = *caller, .reply = std::move(reply), .scratch_root = *root, .abandoned = false};
  {
    const std::scoped_lock lock(controlMutex_);
    preparationControl_ = nullptr;
    preparationCancelRequested_ = false;
  }
  notifyActivity();
  ExecutionContext context;
  context.progress = [this, owner = caller->unique_name](const holonight_packages_domain::ProgressUpdate& update) {
    if (progressForward_) {
      progressForward_(owner, PlanId{}, update);  // a preparation has no plan ID yet (REQ-F-034)
    }
  };
  context.control_changed = [this](ExecutionControl* control) {
    const std::scoped_lock lock(controlMutex_);
    preparationControl_ = control;
    if (control != nullptr && preparationCancelRequested_) {
      static_cast<void>(control->requestCancel());
    }
  };
  auto outcome = std::make_shared<holonight_packages_domain::UpgradePrepareOutcome>();
  runner_->run([this, outcome, context, root = *root] { *outcome = engine_->prepareUpgrade(root, context); },
               [this, outcome] { finishPreparing(std::move(*outcome)); });
}

void PlanService::finishPreparing(holonight_packages_domain::UpgradePrepareOutcome outcome) {
  if (slot_ != Slot::Terminating) {
    slot_ = outcome.worker_stuck ? Slot::Terminating : Slot::Idle;
  }
  {
    const std::scoped_lock lock(controlMutex_);
    preparationControl_ = nullptr;
    preparationCancelRequested_ = false;
  }
  ActivePreparation preparation = std::move(*preparation_);
  preparation_.reset();
  if (preparation.abandoned) {
    notifyActivity();  // the Cancelled reply went out already; in production the process has ended by now
    return;
  }
  if (!outcome.result) {
    scratch_->remove(preparation.scratch_root);
    notifyActivity();
    preparation.reply(std::unexpected(std::move(outcome.result.error())));
    if (outcome.worker_stuck) {
      terminator_->terminateAfterReply();  // a download that cannot be stopped would hold the slot (REQ-F-003)
    }
    return;
  }
  auto plan_id =
      store_.add(preparation.owner, holonight_packages_domain::PlanRequest{.action = PlanAction::Upgrade, .names = {}},
                 std::move(outcome.result->plan),
                 holonight_packages_domain::UpgradeWorkspace{.scratch_root = preparation.scratch_root,
                                                             .guard = std::move(outcome.result->guard)});
  notifyActivity();
  if (!plan_id) {
    scratch_->remove(preparation.scratch_root);
    preparation.reply(std::unexpected(failure(TransactionErrorCode::Busy)));
    return;
  }
  const StoredPlan* stored = store_.peek(*plan_id);
  preparation.reply(Prepared{.id = *plan_id, .plan = stored->plan, .expires_at = stored->created + lifetime_});
}

// REQ-F-032: the preparation changes nothing live, so a cancel always succeeds. libalpm cannot stop the download, so
// the reply goes out and the process ends.
void PlanService::abandonPreparation() {
  {
    const std::scoped_lock lock(controlMutex_);
    if (preparationControl_ != nullptr) {
      static_cast<void>(preparationControl_->requestCancel());
    } else {
      preparationCancelRequested_ = true;
    }
  }
  slot_ = Slot::Terminating;
  preparation_->abandoned = true;
  scratch_->remove(preparation_->scratch_root);
  auto reply = std::move(preparation_->reply);
  notifyActivity();
  reply(std::unexpected(failure(TransactionErrorCode::Cancelled)));
  terminator_->terminateAfterReply();
}

void PlanService::execute(const std::string& unique_name, const PlanId& plan_id, ExecuteReply reply) {
  const auto caller = identify(unique_name);
  if (!caller) {
    reply(ExecutionResult::failed(TransactionErrorCode::PlanNotFound));
    return;
  }
  const PlanLookup lookup = store_.find(plan_id, *caller);
  if (lookup.kind == PlanLookup::Kind::Expired) {
    notifyActivity();
    reply(ExecutionResult::failed(TransactionErrorCode::PlanExpired));
    return;
  }
  if (lookup.kind == PlanLookup::Kind::NotFound) {
    reply(ExecutionResult::failed(TransactionErrorCode::PlanNotFound));
    return;
  }
  if (slot_ != Slot::Idle) {
    reply(ExecutionResult::failed(TransactionErrorCode::Busy));  // the plan stays as it was (REQ-F-009)
    return;
  }
  slot_ = Slot::Authorizing;
  store_.setState(plan_id, PlanState::Authorizing);
  active_ = ActiveExecution{
      .id = plan_id,
      .owner = *caller,
      .request = lookup.plan->request,
      .plan = lookup.plan->plan,
      .reply = std::move(reply),
      .abandoned = false,
      .workspace = lookup.plan->workspace,
  };
  {
    const std::scoped_lock lock(controlMutex_);
    control_ = nullptr;
    cancelRequested_ = false;
    controlRetired_ = false;
  }
  notifyActivity();

  AuthorizationRequest request;
  request.bus_name = caller->unique_name;
  request.allow_user_interaction = true;
  request.cancellation_id = plan_id;
  if (active_->plan.action == PlanAction::Upgrade && active_->plan.upgrade) {
    // Counts only, never names (system-upgrade REQ-F-040).
    const auto& upgrade = *active_->plan.upgrade;
    request.action_id = actions_.upgrade;
    request.details = {{"upgrades", std::to_string(upgrade.upgrades.size())},
                       {"replacements", std::to_string(upgrade.replacements.size())},
                       {"new_packages", std::to_string(upgrade.new_dependencies.size())}};
  } else {
    request.action_id = active_->request.action == PlanAction::Install ? actions_.install : actions_.remove;
    request.details = {{"packages", joinNames(active_->request.names)}};
  }
  authorizer_->check(request, [this, plan_id](AuthorizationDecision decision) {
    if (active_ && active_->id == plan_id && slot_ == Slot::Authorizing) {
      authorizationDecided(decision);
    }
  });
}

void PlanService::authorizationDecided(AuthorizationDecision decision) {
  // Only an explicit approval goes ahead; denial, a challenge and an error are all NotAuthorized (REQ-F-066).
  if (decision != AuthorizationDecision::Authorized) {
    store_.setState(active_->id, PlanState::Pending);
    auto reply = std::move(active_->reply);
    active_.reset();
    slot_ = Slot::Idle;
    notifyActivity();
    reply(ExecutionResult::failed(TransactionErrorCode::NotAuthorized));
    return;
  }
  startRunning();
}

void PlanService::startRunning() {
  slot_ = Slot::Running;
  store_.setState(active_->id, PlanState::Running);
  const PlanId plan_id = active_->id;
  const std::string owner = active_->owner.unique_name;
  auto result = std::make_shared<ExecutionResult>();
  auto request = std::make_shared<holonight_packages_domain::PlanRequest>(active_->request);
  auto plan = std::make_shared<ResolvedPlan>(active_->plan);
  auto workspace = std::make_shared<std::optional<holonight_packages_domain::UpgradeWorkspace>>(active_->workspace);
  ExecutionContext context;
  context.progress = [this, plan_id, owner](const holonight_packages_domain::ProgressUpdate& update) {
    if (progressForward_) {
      progressForward_(owner, plan_id, update);
    }
  };
  context.control_changed = [this](ExecutionControl* control) {
    const std::scoped_lock lock(controlMutex_);
    control_ = control;
    if (control == nullptr) {
      controlRetired_ = true;
    }
    if (control != nullptr && cancelRequested_) {
      static_cast<void>(control->requestCancel());
    }
  };
  runner_->run(
      [this, request, plan, workspace, context, result] {
        *result = plan->action == PlanAction::Upgrade && workspace->has_value()
                      ? engine_->executeUpgrade(*plan, **workspace, context)
                      : engine_->execute(*request, *plan, context);
        const std::scoped_lock lock(controlMutex_);
        control_ = nullptr;
        controlRetired_ = true;
      },
      [this, result] { finishRunning(std::move(*result)); });
}

void PlanService::finishRunning(ExecutionResult result) {
  if (active_->abandoned) {
    // A cancel already replied and consumed the plan; in production the process has ended by now.
    active_.reset();
    {
      const std::scoped_lock lock(controlMutex_);
      control_ = nullptr;
      cancelRequested_ = false;
    }
    notifyActivity();
    return;
  }
  const PlanId plan_id = active_->id;
  // A lock held by another package manager leaves the plan usable (REQ-F-009); every other end consumes it (REQ-F-010).
  const bool retryable =
      result.outcome == ExecutionOutcome::Failed && result.failure.code == TransactionErrorCode::Busy;
  if (retryable) {
    store_.setState(plan_id, PlanState::Pending);
  } else {
    store_.consume(plan_id);
  }
  auto reply = std::move(active_->reply);
  active_.reset();
  {
    const std::scoped_lock lock(controlMutex_);
    control_ = nullptr;
    cancelRequested_ = false;
  }
  slot_ = Slot::Idle;
  notifyActivity();
  reply(std::move(result));
}

void PlanService::finishCancelledDuringAuthorization() {
  store_.consume(active_->id);
  auto reply = std::move(active_->reply);
  active_.reset();
  slot_ = Slot::Idle;
  notifyActivity();
  reply(ExecutionResult::cancelled());
}

void PlanService::abandonRunning() {
  slot_ = Slot::Terminating;
  active_->abandoned = true;
  store_.consume(active_->id);
  auto reply = std::move(active_->reply);
  notifyActivity();
  reply(ExecutionResult::cancelled());  // first the reply ...
  terminator_->terminateAfterReply();   // ... then the process ends (REQ-F-056)
}

std::expected<void, TransactionFailure> PlanService::cancelActive() {
  if (slot_ == Slot::Authorizing) {
    authorizer_->cancel(active_->id);
    finishCancelledDuringAuthorization();
    return {};
  }
  if (!active_ || active_->abandoned) {
    return {};  // already cancelled
  }
  {
    const std::scoped_lock lock(controlMutex_);
    if (controlRetired_) {
      return std::unexpected(failure(TransactionErrorCode::PastPointOfNoReturn));
    }
    if (control_ == nullptr) {
      // The engine has not published its control yet, so nothing can have been committed. It sees the request the
      // moment it publishes.
      cancelRequested_ = true;
    } else if (control_->requestCancel() == CancelDecision::PastPointOfNoReturn) {
      return std::unexpected(failure(TransactionErrorCode::PastPointOfNoReturn));
    }
  }
  abandonRunning();
  return {};
}

std::expected<void, TransactionFailure> PlanService::cancel(const std::string& unique_name, const PlanId& plan_id) {
  if (plan_id.empty()) {
    // Only the caller's own preparation; anyone else learns nothing (REQ-F-033).
    if (!preparation_ || preparation_->abandoned || preparation_->owner.unique_name != unique_name) {
      return std::unexpected(failure(TransactionErrorCode::PlanNotFound));
    }
    abandonPreparation();
    return {};
  }
  const auto caller = identify(unique_name);
  if (!caller) {
    return std::unexpected(failure(TransactionErrorCode::PlanNotFound));
  }
  const PlanLookup lookup = store_.find(plan_id, *caller);
  if (lookup.kind != PlanLookup::Kind::Found) {
    notifyActivity();
    return std::unexpected(failure(TransactionErrorCode::PlanNotFound));
  }
  if (lookup.plan->state == PlanState::Pending) {
    store_.consume(plan_id);
    notifyActivity();
    return {};
  }
  return cancelActive();
}

bool PlanService::shutdownRequested() {
  if (active_ && !active_->abandoned) {
    static_cast<void>(cancelActive());
  }
  if (preparation_ && !preparation_->abandoned) {
    abandonPreparation();
  }
  // A scheduled terminator owns finalization; keep the event loop and its services alive until it runs.
  return slot_ == Slot::Idle;
}

void PlanService::callerGone(const std::string& unique_name) {
  store_.discardPendingFor(unique_name);
  if (preparation_ && !preparation_->abandoned && preparation_->owner.unique_name == unique_name) {
    abandonPreparation();  // REQ-F-035: a disconnect during preparation is a cancel
  }
  if (active_ && active_->owner.unique_name == unique_name && (slot_ == Slot::Authorizing || slot_ == Slot::Running)) {
    static_cast<void>(cancelActive());  // a disconnect is a cancel under the same rules
  }
  notifyActivity();
}

}  // namespace holonight_packages_application
