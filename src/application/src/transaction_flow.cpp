#include "holonight_packages_application/transaction_flow.h"

#include <array>
#include <utility>

namespace holonight_packages_application {

namespace {

using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ServiceError;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

constexpr std::array<FlowAction, 2> kConfirmActions{FlowAction::Confirm, FlowAction::Cancel};
constexpr std::array<FlowAction, 2> kRetryActions{FlowAction::TryAgain, FlowAction::Close};
constexpr std::array<FlowAction, 1> kCloseOnly{FlowAction::Close};

bool isRetryCode(TransactionErrorCode code) {
  return code == TransactionErrorCode::Busy || code == TransactionErrorCode::PlanNotFound ||
         code == TransactionErrorCode::PlanExpired;
}

bool isPreCommitPhase(ProgressPhase phase) {
  return phase == ProgressPhase::Resolving || phase == ProgressPhase::Downloading || phase == ProgressPhase::Verifying;
}

TransactionFailure internalError() {
  return TransactionFailure{.code = TransactionErrorCode::InternalError, .names = {}, .choices = {}};
}

}  // namespace

TransactionFlow::TransactionFlow(holonight_packages_domain::TransactionService& service, PackageListRefresh& refresh)
    : service_(service), refresh_(refresh) {}

TransactionFlow::~TransactionFlow() = default;

void TransactionFlow::setObserver(std::function<void()> changed) { observer_ = std::move(changed); }

void TransactionFlow::setCloseReadyHandler(std::function<void()> ready) { close_ready_ = std::move(ready); }

bool TransactionFlow::allowNewRequest() const noexcept {
  switch (state_) {
    case FlowState::Preparing:
    case FlowState::Previewed:
    case FlowState::Authorizing:
    case FlowState::Executing:
      return false;
    default:
      return true;
  }
}

int TransactionFlow::openDialogCount() const noexcept { return state_ == FlowState::Idle ? 0 : 1; }

bool TransactionFlow::closable() const noexcept {
  return state_ != FlowState::Preparing && state_ != FlowState::Authorizing && state_ != FlowState::Executing;
}

bool TransactionFlow::cancelEnabled() const noexcept { return inRun() && cancel_latch_ && !cancel_pending_; }

bool TransactionFlow::retryable() const noexcept {
  return (state_ == FlowState::Refused || state_ == FlowState::Failed) && failure_ && isRetryCode(failure_->code);
}

std::span<const FlowAction> TransactionFlow::actions() const noexcept {
  switch (state_) {
    case FlowState::Previewed:
      return kConfirmActions;
    case FlowState::Refused:
    case FlowState::Failed:
      return retryable() ? std::span<const FlowAction>(kRetryActions) : std::span<const FlowAction>(kCloseOnly);
    case FlowState::Succeeded:
    case FlowState::Cancelled:
    case FlowState::Unknown:
    case FlowState::HelperUnavailable:
      return kCloseOnly;
    case FlowState::Idle:
    case FlowState::Preparing:
    case FlowState::Authorizing:
    case FlowState::Executing:
      break;
  }
  return {};
}

void TransactionFlow::notify() {
  if (observer_) {
    observer_();
  }
}

void TransactionFlow::requestRefresh() {
  ++refresh_requests_;
  refresh_.refreshAfterOutcome();
}

void TransactionFlow::changeState(FlowState next) {
  state_ = next;
  notify();
}

void TransactionFlow::clearRequestData() {
  prepared_.reset();
  failure_.reset();
  progress_ = ProgressView{};
  cancel_latch_ = false;
  cancel_pending_ = false;
  close_pending_ = false;
  notice_ = FlowNotice::None;
  changed_.clear();
  warnings_.clear();
}

void TransactionFlow::startPrepare() {
  ++generation_;
  clearRequestData();
  state_ = FlowState::Preparing;
  const int generation = generation_;
  const std::weak_ptr<Alive> alive = alive_;
  // Mutate first, notify second: a service that called back synchronously would still see consistent state.
  service_.preparePlan(request_, [this, generation, alive](holonight_packages_domain::PrepareReply reply) {
    if (alive.expired()) {
      return;
    }
    onPrepared(generation, std::move(reply));
  });
  notify();
}

bool TransactionFlow::request(const holonight_packages_domain::PlanRequest& request) {
  if (!allowNewRequest() || request.names.size() != 1 || request.names.front().empty()) {
    return false;
  }
  request_ = request;
  startPrepare();
  return true;
}

void TransactionFlow::tryAgain() {
  if (!retryable()) {
    return;
  }
  startPrepare();
}

void TransactionFlow::confirm() {
  if (state_ != FlowState::Previewed || !prepared_) {
    return;
  }
  cancel_latch_ = true;
  cancel_pending_ = false;
  notice_ = FlowNotice::None;
  progress_ = ProgressView{};
  state_ = FlowState::Authorizing;
  const int generation = generation_;
  const std::weak_ptr<Alive> alive = alive_;
  service_.execute(prepared_->plan_id, [this, generation, alive](holonight_packages_domain::ExecuteReply reply) {
    if (alive.expired()) {
      return;
    }
    onExecuted(generation, std::move(reply));
  });
  notify();
}

void TransactionFlow::dismiss() {
  if (state_ == FlowState::Previewed) {
    if (prepared_) {
      service_.cancel(prepared_->plan_id, [](const holonight_packages_domain::CancelReply&) {});
    }
    ++generation_;
    clearRequestData();
    changeState(FlowState::Idle);
    return;
  }
  if (allowNewRequest() && state_ != FlowState::Idle) {
    ++generation_;
    clearRequestData();
    changeState(FlowState::Idle);
  }
}

void TransactionFlow::cancelRun() {
  if (!cancelEnabled() || !prepared_) {
    return;
  }
  cancel_pending_ = true;
  const int generation = generation_;
  const std::weak_ptr<Alive> alive = alive_;
  service_.cancel(prepared_->plan_id, [this, generation, alive](const holonight_packages_domain::CancelReply& reply) {
    if (alive.expired()) {
      return;
    }
    onCancelled(generation, reply);
  });
  notify();
}

CloseDecision TransactionFlow::closeRequested() {
  switch (state_) {
    case FlowState::Idle:
    case FlowState::Refused:
    case FlowState::Succeeded:
    case FlowState::Failed:
    case FlowState::Cancelled:
    case FlowState::Unknown:
    case FlowState::HelperUnavailable:
    case FlowState::Preparing:
      return CloseDecision::AcceptNow;
    case FlowState::Previewed:
    case FlowState::Authorizing:
    case FlowState::Executing:
      break;
  }
  if (close_pending_) {
    return CloseDecision::DeferUntilCancelReply;
  }
  if (state_ != FlowState::Previewed && !cancel_latch_) {
    notice_ = FlowNotice::CloseUnavailable;
    notify();
    return CloseDecision::Ignore;
  }
  close_pending_ = true;
  if (state_ == FlowState::Previewed || !cancel_pending_) {
    if (prepared_) {
      const int generation = generation_;
      const std::weak_ptr<Alive> alive = alive_;
      if (state_ != FlowState::Previewed) {
        cancel_pending_ = true;
      }
      service_.cancel(prepared_->plan_id,
                      [this, generation, alive](const holonight_packages_domain::CancelReply& reply) {
                        if (alive.expired()) {
                          return;
                        }
                        onCancelled(generation, reply);
                      });
    }
  }
  notify();
  return CloseDecision::DeferUntilCancelReply;
}

void TransactionFlow::resolveDeferredClose() {
  close_pending_ = false;
  if (close_ready_) {
    close_ready_();
  }
}

void TransactionFlow::onProgress(const std::string& plan_id, const holonight_packages_domain::ProgressUpdate& update) {
  if (!inRun() || !prepared_ || plan_id != prepared_->plan_id) {
    return;
  }
  if (progress_.seen && update.phase == progress_.phase && update.current < progress_.current) {
    return;
  }
  progress_ = ProgressView{
      .seen = true,
      .phase = update.phase,
      .current = update.current,
      .total = update.total,
      .item = update.item,
  };
  cancel_latch_ = cancel_latch_ && isPreCommitPhase(update.phase);
  state_ = FlowState::Executing;
  notify();
}

void TransactionFlow::onPrepared(int generation, holonight_packages_domain::PrepareReply reply) {
  if (generation != generation_ || state_ != FlowState::Preparing) {
    return;
  }
  if (reply) {
    prepared_ = std::move(*reply);
    notice_ = FlowNotice::None;
    changeState(FlowState::Previewed);
    return;
  }
  switch (reply.error().kind) {
    case ServiceError::Kind::Refused:
      failure_ = std::move(reply.error().failure);
      changeState(FlowState::Refused);
      return;
    case ServiceError::Kind::HelperUnavailable:
      changeState(FlowState::HelperUnavailable);
      return;
    case ServiceError::Kind::NoReply:
    case ServiceError::Kind::Rejected:
      failure_ = internalError();
      changeState(FlowState::Refused);
      return;
  }
}

void TransactionFlow::failWith(holonight_packages_domain::TransactionFailure failure) {
  failure_ = std::move(failure);
  changed_.clear();
  changeState(FlowState::Failed);
  requestRefresh();
}

void TransactionFlow::onExecuted(int generation, holonight_packages_domain::ExecuteReply reply) {
  if (generation != generation_ || !inRun()) {
    return;
  }
  cancel_pending_ = false;
  if (!reply) {
    switch (reply.error().kind) {
      case ServiceError::Kind::NoReply:
        changeState(FlowState::Unknown);
        requestRefresh();
        return;
      case ServiceError::Kind::HelperUnavailable:
        changeState(FlowState::HelperUnavailable);
        return;
      case ServiceError::Kind::Refused:
      case ServiceError::Kind::Rejected:
        failWith(internalError());
        return;
    }
    return;
  }
  switch (reply->outcome) {
    case ExecutionOutcome::Succeeded:
      changed_ = std::move(reply->changed);
      warnings_ = std::move(reply->warnings);
      changeState(FlowState::Succeeded);
      requestRefresh();
      return;
    case ExecutionOutcome::Cancelled:
      changed_.clear();
      warnings_.clear();
      changeState(FlowState::Cancelled);
      requestRefresh();
      return;
    case ExecutionOutcome::Failed:
      break;
  }
  if (reply->failure.code == TransactionErrorCode::NotAuthorized) {
    notice_ = FlowNotice::NotAuthorized;
    progress_ = ProgressView{};
    changeState(FlowState::Previewed);
    return;
  }
  failure_ = std::move(reply->failure);
  changed_ = std::move(reply->changed);
  changeState(FlowState::Failed);
  requestRefresh();
}

void TransactionFlow::onCancelled(int generation, const holonight_packages_domain::CancelReply& reply) {
  if (generation != generation_) {
    return;
  }
  const bool past_point = !reply && reply.error().kind == ServiceError::Kind::Refused &&
                          reply.error().failure.code == TransactionErrorCode::PastPointOfNoReturn;
  if (past_point) {
    cancel_pending_ = false;
    cancel_latch_ = false;
    notice_ = FlowNotice::CloseUnavailable;
    close_pending_ = false;
    notify();
    return;
  }
  if (close_pending_) {
    resolveDeferredClose();
    return;
  }
  if (reply) {
    return;  // accepted: wait for the Execute outcome
  }
  cancel_pending_ = false;
  notify();
}

}  // namespace holonight_packages_application
