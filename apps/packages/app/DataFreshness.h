#pragma once

#include <QDateTime>
#include <QString>

#include <chrono>

// Presentation of sync-database freshness, shared by the Updates and Explore pages so both show the same wording.
namespace data_freshness {

[[nodiscard]] QDateTime toQDateTime(const std::chrono::system_clock::time_point& time_point);

// "Data as of <local short date-time>"; empty for an invalid time.
[[nodiscard]] QString dataAsOfLabel(const QDateTime& data_as_of);

[[nodiscard]] QString staleHintText();

}  // namespace data_freshness
