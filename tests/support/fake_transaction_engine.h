#pragma once

#include "holonight_packages_domain/transaction_engine.h"

#include <atomic>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <vector>

namespace holonight_packages_testing {

// Scriptable engine for the service tests. By default it resolves to `plan` and executes to `result`.
class FakeTransactionEngine final : public holonight_packages_domain::TransactionEngine {
 public:
  using Plan = holonight_packages_domain::ResolvedPlan;
  using Failure = holonight_packages_domain::TransactionFailure;
  using Request = holonight_packages_domain::PlanRequest;
  using Result = holonight_packages_domain::ExecutionResult;
  using Context = holonight_packages_domain::ExecutionContext;

  // Returned by resolve() unless `resolveScript` is set.
  std::expected<Plan, Failure> resolveResult = Plan{};
  std::function<std::expected<Plan, Failure>(const Request&)> resolveScript;
  // Returned by execute() unless `executeScript` is set.
  Result executeResult = Result::succeeded();
  std::function<Result(const Request&, const Plan&, const Context&)> executeScript;
  // system-upgrade: returned by prepareUpgrade() unless `prepareUpgradeScript` is set, and by executeUpgrade() unless
  // `executeUpgradeScript` is set.
  holonight_packages_domain::UpgradePrepareOutcome prepareUpgradeResult;
  std::function<holonight_packages_domain::UpgradePrepareOutcome(const std::filesystem::path&, const Context&)>
      prepareUpgradeScript;
  Result executeUpgradeResult = Result::succeeded();
  std::function<Result(const Plan&, const holonight_packages_domain::UpgradeWorkspace&, const Context&)>
      executeUpgradeScript;

  [[nodiscard]] std::expected<Plan, Failure> resolve(const Request& request) override {
    ++resolveCalls_;
    requests_.push_back(request);
    return resolveScript ? resolveScript(request) : resolveResult;
  }

  [[nodiscard]] Result execute(const Request& request, const Plan& approved, const Context& context) override {
    ++executeCalls_;
    return executeScript ? executeScript(request, approved, context) : executeResult;
  }

  [[nodiscard]] holonight_packages_domain::UpgradePrepareOutcome prepareUpgrade(
      const std::filesystem::path& scratch_root, const Context& context) override {
    ++prepareUpgradeCalls_;
    {
      const std::scoped_lock lock(scratchMutex_);
      scratchRoots_.push_back(scratch_root);
    }
    return prepareUpgradeScript ? prepareUpgradeScript(scratch_root, context) : prepareUpgradeResult;
  }

  [[nodiscard]] Result executeUpgrade(const Plan& approved,
                                      const holonight_packages_domain::UpgradeWorkspace& workspace,
                                      const Context& context) override {
    ++executeUpgradeCalls_;
    return executeUpgradeScript ? executeUpgradeScript(approved, workspace, context) : executeUpgradeResult;
  }

  [[nodiscard]] int prepareUpgradeCalls() const { return prepareUpgradeCalls_; }
  [[nodiscard]] int executeUpgradeCalls() const { return executeUpgradeCalls_; }
  [[nodiscard]] std::vector<std::filesystem::path> scratchRoots() const {
    const std::scoped_lock lock(scratchMutex_);
    return scratchRoots_;
  }
  [[nodiscard]] int resolveCalls() const { return resolveCalls_; }
  [[nodiscard]] int executeCalls() const { return executeCalls_; }
  [[nodiscard]] const std::vector<Request>& requests() const { return requests_; }

 public:
  std::atomic<int> resolveCalls_{0};
  std::atomic<int> executeCalls_{0};
  std::vector<Request> requests_;
  std::atomic<int> prepareUpgradeCalls_{0};
  std::atomic<int> executeUpgradeCalls_{0};
  mutable std::mutex scratchMutex_;
  std::vector<std::filesystem::path> scratchRoots_;
};

}  // namespace holonight_packages_testing
