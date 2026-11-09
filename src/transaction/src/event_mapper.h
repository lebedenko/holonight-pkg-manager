#pragma once

#include "holonight_packages_domain/transaction_progress.h"

#include <alpm.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace holonight_packages_transaction {

// Turns libalpm events, download callbacks and progress callbacks into the structured Progress updates of REQ-F-049
// (DESIGN 6.7):
//   resolve/check/conflict events          -> Resolving
//   package retrieval, download callbacks  -> Downloading, current = completed package files
//   key/integrity/load/conflict/space      -> Verifying, item = package
//   hook start and hook run                -> Hooks, item = hook, current = position
//   package operations                     -> Installing / Removing, current = packages done
//   finish()                               -> Done 1/1, always last
// `current` never decreases within one occurrence of a phase. Updates are coalesced to one per 100 ms per phase, except
// phase changes and Done, which always go through. Runs on the thread that drives libalpm.
class EventMapper {
 public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  // `installTotal`/`removeTotal` are the package counts of the plan being committed.
  EventMapper(
      holonight_packages_domain::ProgressSink sink, std::size_t installTotal, std::size_t removeTotal,
      Clock clock = [] { return std::chrono::steady_clock::now(); });

  void onEvent(const alpm_event_t& event);
  void onDownload(const std::string& file, alpm_download_event_type_t type, void* data);
  void onProgress(alpm_progress_t progress, const std::string& item, std::size_t total, std::size_t current);
  // The transaction ended successfully.
  void finish();

 private:
  void emit(holonight_packages_domain::ProgressPhase phase, std::uint32_t current, std::uint32_t total,
            const std::string& item, bool force = false);

  holonight_packages_domain::ProgressSink sink_;
  std::size_t installTotal_;
  std::size_t removeTotal_;
  Clock clock_;
  bool started_ = false;
  holonight_packages_domain::ProgressPhase phase_ = holonight_packages_domain::ProgressPhase::Resolving;
  std::uint32_t maxCurrent_ = 0;
  std::chrono::steady_clock::time_point lastEmit_;
  std::uint32_t downloadsDone_ = 0;
  std::uint32_t downloadsTotal_ = 0;
  std::uint32_t packagesDone_ = 0;
  bool finished_ = false;
};

}  // namespace holonight_packages_transaction
