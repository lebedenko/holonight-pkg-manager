#pragma once

#include "holonight_packages_application/authorizer.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace holonight_packages_testing {

// Records every request and answers synchronously, later, or from a script. `beforeAnswer` runs first (REQ-F-044:
// it can mutate the fixture repository between the approval and the re-resolution).
class FakeAuthorizer final : public holonight_packages_application::Authorizer {
 public:
  using Decision = holonight_packages_application::AuthorizationDecision;
  using Request = holonight_packages_application::AuthorizationRequest;

  // The decision given to every check unless `defer` is set.
  Decision answer = Decision::Authorized;
  // Runs before the decision is delivered.
  std::function<void(const Request&)> beforeAnswer;
  // When true the check stays pending until deliver() is called.
  bool defer = false;

  void check(const Request& request, std::function<void(Decision)> done) override {
    requests_.push_back(request);
    if (defer) {
      pending_ = std::move(done);
      return;
    }
    if (beforeAnswer) {
      beforeAnswer(request);
    }
    done(answer);
  }

  void cancel(const std::string& cancellation_id) override {
    cancelled_.push_back(cancellation_id);
    pending_ = nullptr;
  }

  // Delivers `decision` to a deferred check. False when nothing is pending.
  bool deliver(Decision decision) {
    if (!pending_) {
      return false;
    }
    auto done = std::exchange(pending_, nullptr);
    if (beforeAnswer && !requests_.empty()) {
      beforeAnswer(requests_.back());
    }
    done(decision);
    return true;
  }

  [[nodiscard]] const std::vector<Request>& requests() const { return requests_; }
  [[nodiscard]] const std::vector<std::string>& cancelled() const { return cancelled_; }
  [[nodiscard]] bool hasPending() const { return static_cast<bool>(pending_); }

 public:
  std::vector<Request> requests_;
  std::vector<std::string> cancelled_;
  std::function<void(Decision)> pending_;
};

}  // namespace holonight_packages_testing
