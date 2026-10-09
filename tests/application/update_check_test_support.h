#pragma once

#include "holonight_packages_application/update_check_service.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>

#include <chrono>
#include <functional>

namespace holonight_packages_testing {

// Pumps the Qt event loop until the predicate holds or the wait expires.
inline bool pumpUntil(const std::function<bool()>& predicate, int timeoutMs = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate()) {
    if (timer.elapsed() > timeoutMs) {
      return false;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QTest::qWait(1);
  }
  return true;
}

inline bool waitIdle(const holonight_packages_application::UpdateCheckService& service, int timeoutMs = 5000) {
  return pumpUntil([&service] { return !service.status().checking; }, timeoutMs);
}

}  // namespace holonight_packages_testing
