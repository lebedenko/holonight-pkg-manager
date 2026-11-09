#pragma once

#include "holonight_packages_domain/transaction_engine.h"

#include <atomic>
#include <expected>
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

  [[nodiscard]] std::expected<Plan, Failure> resolve(const Request& request) override {
    ++resolveCalls_;
    requests_.push_back(request);
    return resolveScript ? resolveScript(request) : resolveResult;
  }

  [[nodiscard]] Result execute(const Request& request, const Plan& approved, const Context& context) override {
    ++executeCalls_;
    return executeScript ? executeScript(request, approved, context) : executeResult;
  }

  [[nodiscard]] int resolveCalls() const { return resolveCalls_; }
  [[nodiscard]] int executeCalls() const { return executeCalls_; }
  [[nodiscard]] const std::vector<Request>& requests() const { return requests_; }

 public:
  std::atomic<int> resolveCalls_{0};
  std::atomic<int> executeCalls_{0};
  std::vector<Request> requests_;
};

}  // namespace holonight_packages_testing
