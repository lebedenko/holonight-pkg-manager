#include "DBusCallerIdentity.h"

#include <QDBusConnectionInterface>
#include <QDBusReply>

namespace holonight_helper {

std::optional<std::uint32_t> DBusCallerIdentity::uidOf(const std::string& unique_name) {
  const QDBusReply<uint> reply = connection_.interface()->serviceUid(QString::fromStdString(unique_name));
  if (!reply.isValid()) {
    return std::nullopt;
  }
  return reply.value();
}

}  // namespace holonight_helper
