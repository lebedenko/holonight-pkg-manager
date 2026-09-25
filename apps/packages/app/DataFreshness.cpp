#include "DataFreshness.h"

#include <QCoreApplication>
#include <QLocale>

#include <chrono>

namespace data_freshness {

QDateTime toQDateTime(const std::chrono::system_clock::time_point& time_point) {
  return QDateTime::fromMSecsSinceEpoch(
      std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count());
}

QString dataAsOfLabel(const QDateTime& data_as_of) {
  if (!data_as_of.isValid()) {
    return {};
  }
  return QCoreApplication::translate("DataFreshness", "Data as of %1")
      .arg(QLocale().toString(data_as_of.toLocalTime(), QLocale::ShortFormat));
}

QString staleHintText() {
  return QCoreApplication::translate(
      "DataFreshness",
      "Your package databases are out of date. Sync them with your package manager, then press Reload.");
}

}  // namespace data_freshness
