#pragma once

#include <QObject>

namespace holonight_helper {

// Turns SIGTERM and SIGINT into a Qt signal so the helper can leave its event loop normally. It duplicates
// apps/packaged/SignalHandler on purpose (DESIGN 3.1).
class HelperSignals : public QObject {
  Q_OBJECT

 public:
  explicit HelperSignals(QObject* parent = nullptr);
  ~HelperSignals() override;
  HelperSignals(const HelperSignals&) = delete;
  HelperSignals& operator=(const HelperSignals&) = delete;
  HelperSignals(HelperSignals&&) = delete;
  HelperSignals& operator=(HelperSignals&&) = delete;

 signals:
  void terminationRequested();
};

}  // namespace holonight_helper
