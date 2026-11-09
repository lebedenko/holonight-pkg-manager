#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace holonight_packages_application {

enum class SubjectKind : std::uint8_t { SystemBusName };

// What the helper asks the system authorization service (REQ-F-065). The subject is the caller's unique bus name; there
// is deliberately no process-ID field, so a PID-based check cannot be written against this port.
struct AuthorizationRequest {
  SubjectKind subject = SubjectKind::SystemBusName;
  std::string bus_name;
  std::string action_id;
  bool allow_user_interaction = true;
  std::string cancellation_id;  // the plan ID
  std::vector<std::pair<std::string, std::string>> details;

  bool operator==(const AuthorizationRequest&) const = default;
};

enum class AuthorizationDecision : std::uint8_t { Authorized, NotAuthorized, Challenge, Error };

// The decision port. Production: the authorization service on the system bus. Tests: FakeAuthorizer (REQ-C-007).
class Authorizer {
 public:
  Authorizer() = default;
  Authorizer(const Authorizer&) = delete;
  Authorizer& operator=(const Authorizer&) = delete;
  Authorizer(Authorizer&&) = delete;
  Authorizer& operator=(Authorizer&&) = delete;
  virtual ~Authorizer();

  // May call `done` synchronously or later, exactly once, on the thread that owns the authorizer.
  virtual void check(const AuthorizationRequest& request, std::function<void(AuthorizationDecision)> done) = 0;
  // Abandons a pending check; `done` is not called afterwards.
  virtual void cancel(const std::string& cancellation_id) = 0;
};

}  // namespace holonight_packages_application
