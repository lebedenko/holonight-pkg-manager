#pragma once

#include "holonight_packages_domain/transaction_service.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace holonight_packages_testing {

// Scriptable TransactionService. Every call is recorded in order and its completion callback is held until the test
// releases it, so the fake never calls back from inside the call that started it (the port contract).
class FakeTransactionService final : public holonight_packages_domain::TransactionService {
 public:
  using PrepareReply = holonight_packages_domain::PrepareReply;
  using ExecuteReply = holonight_packages_domain::ExecuteReply;
  using CancelReply = holonight_packages_domain::CancelReply;

  struct Call {
    enum class Kind : std::uint8_t { Prepare, Execute, Cancel };
    Kind kind = Kind::Prepare;
    holonight_packages_domain::PlanRequest request;  // Prepare only
    std::string plan_id;                             // Execute and Cancel
  };

  // What queryProtocolVersion() answers, synchronously deferred like every other reply.
  void setVersion(holonight_packages_domain::VersionReply version) { version_ = std::move(version); }

  FakeTransactionService() = default;
  FakeTransactionService(const FakeTransactionService&) = delete;
  FakeTransactionService& operator=(const FakeTransactionService&) = delete;
  FakeTransactionService(FakeTransactionService&&) = delete;
  FakeTransactionService& operator=(FakeTransactionService&&) = delete;
  ~FakeTransactionService() override = default;

  void setProgressHandler(ProgressHandler handler) override { progress_ = std::move(handler); }

  void preparePlan(const holonight_packages_domain::PlanRequest& request, PrepareDone done) override {
    calls_.push_back(Call{.kind = Call::Kind::Prepare, .request = request, .plan_id = {}});
    prepare_.push_back(std::move(done));
  }
  void execute(const std::string& plan_id, ExecuteDone done) override {
    calls_.push_back(Call{.kind = Call::Kind::Execute, .request = {}, .plan_id = plan_id});
    execute_.push_back(std::move(done));
  }
  void cancel(const std::string& plan_id, CancelDone done) override {
    calls_.push_back(Call{.kind = Call::Kind::Cancel, .request = {}, .plan_id = plan_id});
    cancel_.push_back(std::move(done));
  }
  // A protocol probe is a property read, not a transaction call: it is logged apart from calls().
  void queryProtocolVersion(holonight_packages_domain::ProbeMode mode, VersionDone done) override {
    probes_.push_back(mode);
    probe_.push_back(std::move(done));
  }
  // Delivers every held probe with `version`.
  void completeProbes() {
    while (!probe_.empty()) {
      VersionDone done = std::move(probe_.front());
      probe_.erase(probe_.begin());
      done(version_);
    }
  }
  [[nodiscard]] std::size_t probeCalls() const { return probes_.size(); }
  [[nodiscard]] const std::vector<holonight_packages_domain::ProbeMode>& probes() const { return probes_; }
  [[nodiscard]] std::size_t upgradePrepareCalls() const {
    std::size_t total = 0;
    for (const auto& call : calls_) {
      total += call.kind == Call::Kind::Prepare && call.request.action == holonight_packages_domain::PlanAction::Upgrade
                   ? 1
                   : 0;
    }
    return total;
  }

  // Delivers the oldest held completion of that kind. Returns false when none is held.
  bool completePrepare(PrepareReply reply) { return complete(prepare_, std::move(reply)); }
  bool completeExecute(ExecuteReply reply) { return complete(execute_, std::move(reply)); }
  bool completeCancel(CancelReply reply) { return complete(cancel_, std::move(reply)); }

  void emitProgress(const std::string& plan_id, const holonight_packages_domain::ProgressUpdate& update) const {
    if (progress_) {
      progress_(plan_id, update);
    }
  }

  [[nodiscard]] const std::vector<Call>& calls() const { return calls_; }
  [[nodiscard]] std::size_t count(Call::Kind kind) const {
    std::size_t total = 0;
    for (const auto& call : calls_) {
      total += call.kind == kind ? 1 : 0;
    }
    return total;
  }
  [[nodiscard]] std::size_t prepareCalls() const { return count(Call::Kind::Prepare); }
  [[nodiscard]] std::size_t executeCalls() const { return count(Call::Kind::Execute); }
  [[nodiscard]] std::size_t cancelCalls() const { return count(Call::Kind::Cancel); }
  [[nodiscard]] std::size_t heldPrepare() const { return prepare_.size(); }
  [[nodiscard]] std::size_t heldExecute() const { return execute_.size(); }
  [[nodiscard]] std::size_t heldCancel() const { return cancel_.size(); }

  // Common replies.
  [[nodiscard]] static PrepareReply previewOf(std::string plan_id, holonight_packages_domain::ResolvedPlan plan) {
    return holonight_packages_domain::PreparedPlan{
        .plan_id = std::move(plan_id),
        .plan = std::move(plan),
        .expires_at = {},
    };
  }
  [[nodiscard]] static holonight_packages_domain::ServiceError refusalError(
      holonight_packages_domain::TransactionErrorCode code, std::vector<std::string> names = {}) {
    return holonight_packages_domain::ServiceError{
        .kind = holonight_packages_domain::ServiceError::Kind::Refused,
        .failure =
            holonight_packages_domain::TransactionFailure{
                .code = code,
                .names = std::move(names),
                .choices = {},
            },
    };
  }
  [[nodiscard]] static PrepareReply refusal(holonight_packages_domain::TransactionErrorCode code,
                                            std::vector<std::string> names = {}) {
    return std::unexpected(refusalError(code, std::move(names)));
  }
  [[nodiscard]] static ExecuteReply failed(holonight_packages_domain::TransactionErrorCode code,
                                           std::vector<holonight_packages_domain::ChangedPackage> changed = {}) {
    auto result = holonight_packages_domain::ExecutionResult::failed(code);
    result.changed = std::move(changed);
    return result;
  }

 private:
  holonight_packages_domain::VersionReply version_ = std::uint32_t{2};
  template <typename Done, typename Reply>
  static bool complete(std::vector<Done>& held, Reply reply) {
    if (held.empty()) {
      return false;
    }
    Done done = std::move(held.front());
    held.erase(held.begin());
    done(std::move(reply));
    return true;
  }

  ProgressHandler progress_;
  std::vector<Call> calls_;
  std::vector<PrepareDone> prepare_;
  std::vector<ExecuteDone> execute_;
  std::vector<CancelDone> cancel_;
  std::vector<VersionDone> probe_;
  std::vector<holonight_packages_domain::ProbeMode> probes_;
};

}  // namespace holonight_packages_testing
