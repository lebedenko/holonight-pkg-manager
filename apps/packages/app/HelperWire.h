#pragma once

#include "HelperProtocol.h"
#include "holonight_packages_domain/transaction_service.h"

#include <QDBusError>
#include <QDBusMessage>

#include <optional>
#include <string_view>

namespace helper_wire {

// Pure decoders: they never throw and never hand a raw helper string to the caller. A reply that cannot be trusted
// (wrong argument count or signature, an unparseable change) gives ServiceError::Kind::NoReply, which the flow shows
// as Unknown for Execute. An unknown status code gives InternalError with no names (REQ-F-047).
// `expected_action` is the wire name of the requested action ("install" or "remove").
[[nodiscard]] holonight_packages_domain::PrepareReply decodePrepareReply(const QDBusMessage& reply,
                                                                         std::string_view expected_action);
// The reply of PrepareUpgrade: status, plan ID and the upgrade preview (system-upgrade REQ-F-019).
[[nodiscard]] holonight_packages_domain::PrepareReply decodePrepareUpgradeReply(const QDBusMessage& reply);
// The ProtocolVersion property read through org.freedesktop.DBus.Properties.Get.
[[nodiscard]] holonight_packages_domain::VersionReply decodeProtocolVersionReply(const QDBusMessage& reply);
[[nodiscard]] holonight_packages_domain::ExecuteReply decodeExecuteReply(const QDBusMessage& reply);
[[nodiscard]] holonight_packages_domain::CancelReply decodeCancelReply(const QDBusMessage& reply);
[[nodiscard]] std::optional<holonight_packages_domain::ProgressUpdate> decodeProgress(const QString& phase,
                                                                                      uint current, uint total,
                                                                                      const QString& item);
// Bus error -> ServiceError (DESIGN section 6.4).
[[nodiscard]] holonight_packages_domain::ServiceError classifyBusError(const QDBusError& error);

}  // namespace helper_wire
