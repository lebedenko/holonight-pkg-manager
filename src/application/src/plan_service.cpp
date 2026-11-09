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
      lifetime_(lifetime) {}

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
  request.action_id = active_->request.action == PlanAction::Install ? actions_.install : actions_.remove;
  request.allow_user_interaction = true;
  request.cancellation_id = plan_id;
  request.details = {{"packages", joinNames(active_->request.names)}};
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
      [this, request, plan, context, result] {
        *result = engine_->execute(*request, *plan, context);
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
    slot_ = Slot::Idle;
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
  return slot_ == Slot::Idle;
}

void PlanService::callerGone(const std::string& unique_name) {
  store_.discardPendingFor(unique_name);
  if (active_ && active_->owner.unique_name == unique_name && (slot_ == Slot::Authorizing || slot_ == Slot::Running)) {
    static_cast<void>(cancelActive());  // a disconnect is a cancel under the same rules
  }
  notifyActivity();
}

}  // namespace holonight_packages_application
