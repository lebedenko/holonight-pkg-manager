#pragma once

#include "holonight_packages_application/update_check_policy.h"
#include "holonight_packages_application/update_check_ports.h"
#include "holonight_packages_application/update_check_status.h"
#include "holonight_packages_domain/backend_capabilities.h"
#include "holonight_packages_domain/update_checker.h"
#include "holonight_packages_domain/update_snapshot_store.h"

#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace holonight_packages_application {

// Coordinates online update checks: single-flight, join, on-demand cooldown, last-good snapshot and persistence.
// Instantiated only in holonight-packaged. All state lives on the thread that owns the object (the home thread);
// the checker runs on a private one-thread pool.
class UpdateCheckService : public QObject {
  Q_OBJECT

 public:
  struct Dependencies {
    std::shared_ptr<holonight_packages_domain::UpdateChecker> checker;
    std::shared_ptr<holonight_packages_domain::UpdateSnapshotStore> store;
    std::shared_ptr<Clock> clock;
    holonight_packages_domain::BackendCapabilities capabilities;
  };

  UpdateCheckService(Dependencies dependencies, UpdateCheckPolicy policy, QObject* parent = nullptr);
  ~UpdateCheckService() override;

  UpdateCheckService(const UpdateCheckService&) = delete;
  UpdateCheckService& operator=(const UpdateCheckService&) = delete;
  UpdateCheckService(UpdateCheckService&&) = delete;
  UpdateCheckService& operator=(UpdateCheckService&&) = delete;

  // Loads the persisted snapshot and emits snapshotAdopted when one is valid. When the load reports an invalid file,
  // calls store->discardInvalid(): this service is the single writer.
  void start();

  // Thread-safe. Joins a running check; an OnDemand request while checking or inside the cooldown is a no-op; a
  // backend without the capability ignores the request. onDone runs on the home thread with the shared outcome.
  void requestCheck(CheckOrigin origin, std::function<void(const UpdateCheckOutcome&)> onDone = {});

  [[nodiscard]] const UpdateCheckStatus& status() const { return status_; }
  [[nodiscard]] const std::optional<holonight_packages_domain::CheckedSnapshot>& lastGood() const { return last_good_; }
  [[nodiscard]] bool canCheckForUpdates() const { return dependencies_.capabilities.canCheckForUpdates; }

 signals:
  void statusChanged(const holonight_packages_application::UpdateCheckStatus& status);
  // Exactly once per completed check, after the status has been updated.
  void checkCompleted(const holonight_packages_application::UpdateCheckOutcome& outcome);
  // A successful check, or the valid snapshot loaded at start.
  void snapshotAdopted(const holonight_packages_domain::CheckedSnapshot& snapshot);

 private:
  struct WorkerResult {
    std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateCheckError> result;
    // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
    std::chrono::system_clock::time_point completedAt;
    // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
    std::optional<std::string> saveError;
  };

  void handleRequest(CheckOrigin origin, std::function<void(const UpdateCheckOutcome&)> onDone);
  void beginCheck(CheckOrigin origin);
  void onWorkerFinished();
  void setStatus(const UpdateCheckStatus& status);
  void adoptSnapshot(const holonight_packages_domain::CheckedSnapshot& snapshot);

  Dependencies dependencies_;
  UpdateCheckPolicy policy_;
  QThreadPool pool_;
  QFutureWatcher<WorkerResult> watcher_;
  UpdateCheckStatus status_;
  std::optional<holonight_packages_domain::CheckedSnapshot> last_good_;
  std::vector<std::function<void(const UpdateCheckOutcome&)>> joined_;
  std::optional<std::chrono::system_clock::time_point> last_completed_;
  CheckOrigin running_origin_ = CheckOrigin::Automatic;
  std::chrono::steady_clock::time_point started_at_;
  bool running_ = false;
};

}  // namespace holonight_packages_application
