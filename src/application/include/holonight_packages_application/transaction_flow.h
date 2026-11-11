#pragma once

#include "holonight_packages_application/package_list_refresh.h"
#include "holonight_packages_domain/transaction_service.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace holonight_packages_application {

// REQ-F-007: exactly these eleven states.
enum class FlowState : std::uint8_t {
  Idle,
  Preparing,
  Previewed,
  Refused,
  Authorizing,
  Executing,
  Succeeded,
  Failed,
  Cancelled,
  Unknown,
  HelperUnavailable,
};

// The buttons a result or confirmation page may offer (REQ-F-021..024). The run-time Cancel control of the progress
// page is not here: it is cancelEnabled()/cancelling().
enum class FlowAction : std::uint8_t { Confirm, Cancel, TryAgain, Close };

// What the window may do when the user asks to close it (REQ-F-033..035).
enum class CloseDecision : std::uint8_t {
  AcceptNow,              // nothing to wait for
  DeferUntilCancelReply,  // a Cancel call is in flight; the close-ready handler runs when it ends
  Ignore,                 // past the point of no return: keep the window open
};

// Why a message is shown besides the state's own text.
enum class FlowNotice : std::uint8_t { None, NotAuthorized, CloseUnavailable };

struct ProgressView {
  bool seen = false;  // at least one event applied for this execution
  holonight_packages_domain::ProgressPhase phase = holonight_packages_domain::ProgressPhase::Resolving;
  std::uint32_t current = 0;
  std::uint32_t total = 0;
  std::string item;
};

// The state machine of one install or remove request, from preparation to result. Qt-free and single-threaded: every
// method and every service callback runs on the GUI thread. DESIGN sections 4.3 and 5 define the transitions (T1..T27).
class TransactionFlow {
 public:
  TransactionFlow(holonight_packages_domain::TransactionService& service, PackageListRefresh& refresh);
  ~TransactionFlow();  // invalidates callbacks still held by the service
  TransactionFlow(const TransactionFlow&) = delete;
  TransactionFlow& operator=(const TransactionFlow&) = delete;
  TransactionFlow(TransactionFlow&&) = delete;
  TransactionFlow& operator=(TransactionFlow&&) = delete;

  void setObserver(std::function<void()> changed);         // after every state or data change
  void setCloseReadyHandler(std::function<void()> ready);  // after a deferred close may proceed

  // User intents. Each does nothing when the current state does not allow it (REQ-F-030).
  bool request(const holonight_packages_domain::PlanRequest& request);  // REQ-F-001, 002
  void confirm();                                                       // Previewed -> Authorizing (REQ-F-005, 026)
  void dismiss();    // Previewed: Cancel call, -> Idle; terminal states: -> Idle  (REQ-F-006)
  void cancelRun();  // Authorizing/Executing with cancelEnabled                 (REQ-F-013)
  void tryAgain();   // Refused/Failed with Busy, PlanNotFound, PlanExpired      (REQ-F-023)
  [[nodiscard]] CloseDecision closeRequested();  // window close request        (REQ-F-033..036)
  // A Progress signal from the service (the controller connects the service's handler to this).
  void onProgress(const std::string& plan_id, const holonight_packages_domain::ProgressUpdate& update);

  [[nodiscard]] FlowState state() const noexcept { return state_; }
  [[nodiscard]] bool allowNewRequest() const noexcept;  // REQ-F-029: true in Idle and the terminal result states
  [[nodiscard]] int openDialogCount() const noexcept;   // 0 in Idle, otherwise 1 (REQ-F-031)
  [[nodiscard]] bool closable() const noexcept;         // REQ-F-032: false in Preparing, Authorizing, Executing
  [[nodiscard]] const holonight_packages_domain::PlanRequest& currentRequest() const noexcept { return request_; }
  [[nodiscard]] const std::optional<holonight_packages_domain::PreparedPlan>& prepared() const noexcept {
    return prepared_;
  }
  [[nodiscard]] const std::optional<holonight_packages_domain::TransactionFailure>& failure() const noexcept {
    return failure_;
  }
  [[nodiscard]] const ProgressView& progress() const noexcept { return progress_; }
  [[nodiscard]] bool cancelEnabled() const noexcept;  // REQ-F-012 (the "cancel flag")
  [[nodiscard]] bool cancelling() const noexcept { return cancel_pending_ && inRun(); }  // REQ-F-013
  [[nodiscard]] FlowNotice notice() const noexcept { return notice_; }
  [[nodiscard]] const std::vector<holonight_packages_domain::ChangedPackage>& changed() const noexcept {
    return changed_;
  }
  [[nodiscard]] const std::vector<holonight_packages_domain::WarningToken>& warnings() const noexcept {
    return warnings_;
  }
  [[nodiscard]] std::span<const FlowAction> actions() const noexcept;  // REQ-F-021..024
  [[nodiscard]] int refreshRequests() const noexcept { return refresh_requests_; }

 private:
  struct Alive {};

  [[nodiscard]] bool inRun() const noexcept {
    return state_ == FlowState::Authorizing || state_ == FlowState::Executing;
  }
  [[nodiscard]] bool retryable() const noexcept;
  void startPrepare();
  void changeState(FlowState next);
  void notify();
  void requestRefresh();
  void onPrepared(int generation, holonight_packages_domain::PrepareReply reply);
  void onExecuted(int generation, holonight_packages_domain::ExecuteReply reply);
  void onCancelled(int generation, const holonight_packages_domain::CancelReply& reply);
  void resolveDeferredClose();
  void clearRequestData();
  void failWith(holonight_packages_domain::TransactionFailure failure);

  holonight_packages_domain::TransactionService& service_;
  PackageListRefresh& refresh_;
  std::shared_ptr<Alive> alive_ = std::make_shared<Alive>();
  std::function<void()> observer_;
  std::function<void()> close_ready_;

  FlowState state_ = FlowState::Idle;
  holonight_packages_domain::PlanRequest request_;
  std::optional<holonight_packages_domain::PreparedPlan> prepared_;
  std::optional<holonight_packages_domain::TransactionFailure> failure_;
  ProgressView progress_;
  bool cancel_latch_ = false;    // true from Authorizing until the first phase at or after Hooks
  bool cancel_pending_ = false;  // a Cancel call is in flight for the running plan
  bool close_pending_ = false;   // a window close waits for a Cancel reply
  FlowNotice notice_ = FlowNotice::None;
  std::vector<holonight_packages_domain::ChangedPackage> changed_;
  std::vector<holonight_packages_domain::WarningToken> warnings_;
  int generation_ = 0;
  int refresh_requests_ = 0;
};

}  // namespace holonight_packages_application
