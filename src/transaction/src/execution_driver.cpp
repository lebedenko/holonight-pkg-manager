#include "execution_driver.h"

#include "fetch_seam.h"
#include "plan_resolver.h"

#include <algorithm>
#include <string>
#include <vector>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::ChangeAction;
using holonight_packages_domain::ChangedPackage;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_domain::WarningToken;

// REQ-F-045: a re-resolution that refuses means the plan changed, except for failures that say something about the
// environment and not about the plan.
TransactionErrorCode reresolutionCode(TransactionErrorCode code) {
  switch (code) {
    case TransactionErrorCode::Busy:
    case TransactionErrorCode::InternalError:
    case TransactionErrorCode::ConfigurationError:
    case TransactionErrorCode::DatabaseUnavailable:
    case TransactionErrorCode::InsecureSigLevel:
    case TransactionErrorCode::NeedsDecision:  // REQ-F-053: a decision at Execute time stays a decision
      return code;
    default:
      return TransactionErrorCode::PlanChanged;
  }
}

}  // namespace

ExecutionDriver::ExecutionDriver(const TransactionConfig& config, LogSink journal, const TransactionSeams& seams,
                                 const holonight_packages_domain::PlanRequest& request,
                                 const holonight_packages_domain::ResolvedPlan& approved,
                                 const holonight_packages_domain::ExecutionContext& context)
    : config_(&config),
      journal_(std::move(journal)),
      seams_(&seams),
      request_(&request),
      approved_(&approved),
      context_(&context),
      reporter_([&approved] {
        std::vector<std::string> names;
        names.reserve(approved.changes.size());
        for (const auto& change : approved.changes) {
          names.push_back(change.name);
        }
        return names;
      }()) {}

void ExecutionDriver::observe(std::string_view stage) const {
  if (seams_->observe) {
    seams_->observe(stage);
  }
}

ExecutionResult ExecutionDriver::run() {
  auto session = AlpmSession::open(*config_, request_->action, journal_);
  if (!session) {
    return ExecutionResult::failed(std::move(session.error()));
  }
  handle_ = (*session)->handle();
  installFetchSeam(handle_, *seams_);
  if (context_->control_changed) {
    context_->control_changed(&gate_);
  }
  class Unpublish {
   public:
    explicit Unpublish(const holonight_packages_domain::ExecutionContext* context) : context_(context) {}
    Unpublish(const Unpublish&) = delete;
    Unpublish& operator=(const Unpublish&) = delete;
    Unpublish(Unpublish&&) = delete;
    Unpublish& operator=(Unpublish&&) = delete;
    ~Unpublish() {
      if (context_->control_changed) {
        context_->control_changed(nullptr);
      }
    }

   private:
    const holonight_packages_domain::ExecutionContext* context_;
  } unpublish(context_);
  observe("opened");

  std::size_t installs = 0;
  std::size_t removals = 0;
  for (const auto& change : approved_->changes) {
    (approved_->isRemoval() || change.kind == holonight_packages_domain::ChangeKind::Orphan ? removals : installs) += 1;
  }
  mapper_ = std::make_unique<EventMapper>(context_->progress, installs, removals);
  (*session)->setEventHandler([this](const alpm_event_t& event) {
    onEvent(event);
    mapper_->onEvent(event);
  });
  (*session)->setDownloadHandler([this](const std::string& file, alpm_download_event_type_t type, void* data) {
    mapper_->onDownload(file, type, data);
  });
  (*session)->setProgressHandler([this](alpm_progress_t progress, const std::string& item, int, std::size_t total,
                                        std::size_t current) { mapper_->onProgress(progress, item, total, current); });

  for (int number = 1;; ++number) {
    Attempt pass = attempt(**session, number);
    if (!pass.retry) {
      observe("finished");
      return std::move(pass.result);
    }
    // The second pass starts from nothing: no recorded questions, no stage, and a second corruption is not repaired.
    journal_(LogLevel::Info, "a corrupt package file was deleted; repeating the execution once");
    (*session)->questions().clear();
    (*session)->questions().setCorruptedAnswer(false);
    stage_ = CommitStage::BeforeCommitPoint;
    changed_.clear();
    warnings_.clear();
    interrupted_ = false;
  }
}

ExecutionDriver::Attempt ExecutionDriver::attempt(AlpmSession& session, int number) {
  PlanResolver resolver(session, journal_);
  auto resolved = resolver.resolve(*request_, ResolveMode::Execute);
  if (!resolved) {
    TransactionFailure failure = std::move(resolved.error());
    failure.code = reresolutionCode(failure.code);
    if (failure.code == TransactionErrorCode::PlanChanged) {
      journal_(LogLevel::Info, "re-resolution refused the plan; reported as PlanChanged");
      failure.names.clear();
      failure.choices.clear();
    }
    return Attempt{.result = ExecutionResult::failed(std::move(failure)), .retry = false};
  }
  observe("resolved");
  if (const int fault = seams_->inject_fault ? seams_->inject_fault("resolved") : 0; fault != 0) {
    ExecutionResult injected = failureResult(session, static_cast<alpm_errno_t>(fault), nullptr);
    session.releaseTransaction();
    return Attempt{.result = std::move(injected), .retry = false};
  }
  if (!holonight_packages_domain::samePlan(*approved_, *resolved)) {
    session.releaseTransaction();
    return Attempt{.result = ExecutionResult::failed(TransactionErrorCode::PlanChanged), .retry = false};
  }
  ExecutionResult result = commit(session);
  const bool corrupted = !session.questions().corruptedFiles().empty() &&
                         (session.questions().lastCorruptedReason() == ALPM_ERR_PKG_INVALID_CHECKSUM ||
                          session.questions().lastCorruptedReason() == ALPM_ERR_PKG_INVALID ||
                          session.questions().lastCorruptedReason() == ALPM_ERR_PKG_OPEN);
  const bool retry = number == 1 && result.outcome == holonight_packages_domain::ExecutionOutcome::Failed &&
                     stage_ == CommitStage::BeforeCommitPoint && corrupted;
  session.releaseTransaction();
  return Attempt{.result = std::move(result), .retry = retry};
}

void ExecutionDriver::onEvent(const alpm_event_t& event) {
  switch (event.type) {
    case ALPM_EVENT_HOOK_START:
    case ALPM_EVENT_TRANSACTION_START: {
      const bool preHook = event.type == ALPM_EVENT_HOOK_START && event.hook.when == ALPM_HOOK_PRE_TRANSACTION;
      const bool postHook = event.type == ALPM_EVENT_HOOK_START && !preHook;
      if (postHook || stage_ != CommitStage::BeforeCommitPoint) {
        break;
      }
      // The commit point: from here libalpm may run external code or change the root (DESIGN 6.7).
      stage_ = CommitStage::AtCommitPoint;
      observe("commit-point");
      if (!gate_.tryCommit()) {
        // A cancel won the race. libalpm honours an interrupt only from TRANSACTION_START on (spike S-1), so a hook
        // start is followed by the transaction start, where this is tried again.
        if (!interrupted_) {
          interrupted_ = alpm_trans_interrupt(handle_) == 0;
        }
      } else {
        observe("committed");
      }
      break;
    }
    case ALPM_EVENT_PKG_RETRIEVE_START:
      observe("downloading");
      break;
    case ALPM_EVENT_PACKAGE_OPERATION_START:
      stage_ = CommitStage::AfterChange;
      break;
    case ALPM_EVENT_PACKAGE_OPERATION_DONE: {
      const auto& operation = event.package_operation;
      ChangedPackage changed;
      if (operation.operation == ALPM_PACKAGE_REMOVE) {
        changed = ChangedPackage{
            .name = alpm_pkg_get_name(operation.oldpkg),
            .action = ChangeAction::Remove,
            .old_version = alpm_pkg_get_version(operation.oldpkg),
            .new_version = {},
        };
      } else {
        changed = ChangedPackage{
            .name = alpm_pkg_get_name(operation.newpkg),
            .action = ChangeAction::Install,
            .old_version = operation.oldpkg != nullptr ? alpm_pkg_get_version(operation.oldpkg) : "",
            .new_version = alpm_pkg_get_version(operation.newpkg),
        };
      }
      changed_.push_back(std::move(changed));
      if (const int fault = seams_->inject_fault ? seams_->inject_fault("package-done") : 0;
          fault != 0 && injectedFault_ == 0) {
        injectedFault_ = fault;
        static_cast<void>(alpm_trans_interrupt(handle_));  // the remaining packages are not installed
      }
      break;
    }
    default:
      break;
  }
}

ExecutionResult ExecutionDriver::commit(AlpmSession& session) {
  // A cancel that arrived while the re-resolution ran never starts the commit.
  if (gate_.cancelRequested()) {
    return ExecutionResult::cancelled();
  }
  alpm_list_t* data = nullptr;
  const int status = alpm_trans_commit(handle_, &data);
  if (status == 0 && injectedFault_ != 0) {
    return failureResult(session, static_cast<alpm_errno_t>(injectedFault_), nullptr);
  }
  if (status == 0) {
    if (interrupted_) {
      return ExecutionResult::cancelled();
    }
    if (logMentions(session.logLines(), "command failed to execute correctly")) {
      warnings_.push_back(WarningToken::HookFailed);
    }
    mapper_->finish();
    ExecutionResult result = ExecutionResult::succeeded(std::move(changed_));
    result.warnings = std::move(warnings_);
    return result;
  }
  return failureResult(session, alpm_errno(handle_), data);
}

ExecutionResult ExecutionDriver::failureResult(AlpmSession& session, alpm_errno_t error, alpm_list_t* data) const {
  return reporter_.failureResult(session, error, data, stage_, changed_);
}

}  // namespace holonight_packages_transaction
