#pragma once

#include "holonight_packages_application/caller_identity.h"

#include <QDBusConnection>

namespace holonight_helper {

// Finds the Unix user behind a bus connection with org.freedesktop.DBus.GetConnectionUnixUser (REQ-F-004).
class DBusCallerIdentity final : public holonight_packages_application::CallerIdentityResolver {
 public:
  explicit DBusCallerIdentity(QDBusConnection connection) : connection_(std::move(connection)) {}
  [[nodiscard]] std::optional<std::uint32_t> uidOf(const std::string& unique_name) override;

 private:
  QDBusConnection connection_;
};

}  // namespace holonight_helper
