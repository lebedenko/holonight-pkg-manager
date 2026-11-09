#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace holonight_packages_transaction {

enum class LogLevel : unsigned char { Error, Warning, Info };

// Where raw libalpm text and operational lines go. Production: the journal. Raw libalpm text never reaches a D-Bus
// reply (REQ-F-052).
using LogSink = std::function<void(LogLevel, const std::string&)>;

// Replaces the userinfo of every URL in `text` ("scheme://user:secret@host/..." becomes "scheme://***@host/...").
// Applied to every line before it is logged (REQ-NF-004).
[[nodiscard]] std::string redactUrlCredentials(std::string_view text);

}  // namespace holonight_packages_transaction
