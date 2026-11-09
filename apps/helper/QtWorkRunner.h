#pragma once

#include "holonight_packages_application/work_runner.h"

#include <QObject>

namespace holonight_helper {

// Runs work on the global thread pool and delivers the completion on the thread that owns this object.
class QtWorkRunner final : public QObject, public holonight_packages_application::WorkRunner {
  Q_OBJECT

 public:
  explicit QtWorkRunner(QObject* parent = nullptr) : QObject(parent) {}
  void run(std::function<void()> work, std::function<void()> done) override;
};

}  // namespace holonight_helper
