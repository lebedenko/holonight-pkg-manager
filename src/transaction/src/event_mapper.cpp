#include "event_mapper.h"

#include <algorithm>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::ProgressPhase;

constexpr std::chrono::milliseconds kCoalesce{100};

std::uint32_t clamp(std::size_t value) { return static_cast<std::uint32_t>(std::min<std::size_t>(value, UINT32_MAX)); }

bool isSignatureFile(const std::string& file) { return file.ends_with(".sig"); }

}  // namespace

EventMapper::EventMapper(holonight_packages_domain::ProgressSink sink, std::size_t installTotal,
                         std::size_t removeTotal, Clock clock)
    : sink_(std::move(sink)), installTotal_(installTotal), removeTotal_(removeTotal), clock_(std::move(clock)) {}

void EventMapper::emit(ProgressPhase phase, std::uint32_t current, std::uint32_t total, const std::string& item,
                       bool force) {
  if (!sink_ || finished_) {
    return;
  }
  const auto now = clock_();
  const bool phaseChange = !started_ || phase != phase_;
  if (phaseChange) {
    phase_ = phase;
    maxCurrent_ = 0;
  }
  maxCurrent_ = std::max(maxCurrent_, current);  // never decreases within one occurrence of a phase
  if (!phaseChange && !force && now - lastEmit_ < kCoalesce) {
    return;
  }
  started_ = true;
  lastEmit_ = now;
  sink_(
      holonight_packages_domain::ProgressUpdate{.phase = phase, .current = maxCurrent_, .total = total, .item = item});
}

void EventMapper::onEvent(const alpm_event_t& event) {
  switch (event.type) {
    case ALPM_EVENT_CHECKDEPS_START:
    case ALPM_EVENT_RESOLVEDEPS_START:
    case ALPM_EVENT_INTERCONFLICTS_START:
      emit(ProgressPhase::Resolving, 0, 0, {});
      break;
    case ALPM_EVENT_PKG_RETRIEVE_START:
      downloadsDone_ = 0;
      downloadsTotal_ = clamp(event.pkg_retrieve.num);
      emit(ProgressPhase::Downloading, 0, downloadsTotal_, {});
      break;
    case ALPM_EVENT_KEYRING_START:
    case ALPM_EVENT_INTEGRITY_START:
    case ALPM_EVENT_LOAD_START:
    case ALPM_EVENT_FILECONFLICTS_START:
    case ALPM_EVENT_DISKSPACE_START:
      emit(ProgressPhase::Verifying, 0, clamp(installTotal_ + removeTotal_), {});
      break;
    case ALPM_EVENT_HOOK_RUN_START:
      emit(ProgressPhase::Hooks, clamp(event.hook_run.position), clamp(event.hook_run.total),
           event.hook_run.name != nullptr ? event.hook_run.name : "", true);
      break;
    case ALPM_EVENT_HOOK_RUN_DONE:
      break;
    case ALPM_EVENT_TRANSACTION_START:
      packagesDone_ = 0;
      break;
    case ALPM_EVENT_PACKAGE_OPERATION_START: {
      const bool removal = event.package_operation.operation == ALPM_PACKAGE_REMOVE;
      alpm_pkg_t* package = removal ? event.package_operation.oldpkg : event.package_operation.newpkg;
      emit(removal ? ProgressPhase::Removing : ProgressPhase::Installing, packagesDone_,
           clamp(removal ? removeTotal_ : installTotal_), package != nullptr ? alpm_pkg_get_name(package) : "", true);
      break;
    }
    case ALPM_EVENT_PACKAGE_OPERATION_DONE: {
      ++packagesDone_;
      const bool removal = event.package_operation.operation == ALPM_PACKAGE_REMOVE;
      alpm_pkg_t* package = removal ? event.package_operation.oldpkg : event.package_operation.newpkg;
      emit(removal ? ProgressPhase::Removing : ProgressPhase::Installing, packagesDone_,
           clamp(removal ? removeTotal_ : installTotal_), package != nullptr ? alpm_pkg_get_name(package) : "", true);
      break;
    }
    default:
      break;
  }
}

void EventMapper::onDownload(const std::string& file, alpm_download_event_type_t type, void* data) {
  if (isSignatureFile(file)) {
    return;  // only package files count towards the retrieve total
  }
  switch (type) {
    case ALPM_DOWNLOAD_INIT:
    case ALPM_DOWNLOAD_PROGRESS:
    case ALPM_DOWNLOAD_RETRY:
      emit(ProgressPhase::Downloading, downloadsDone_, downloadsTotal_, file);
      break;
    case ALPM_DOWNLOAD_COMPLETED: {
      const auto* completed = static_cast<const alpm_download_event_completed_t*>(data);
      if (completed != nullptr && completed->result >= 0) {
        ++downloadsDone_;
      }
      emit(ProgressPhase::Downloading, downloadsDone_, downloadsTotal_, file, true);
      break;
    }
  }
}

void EventMapper::onProgress(alpm_progress_t progress, const std::string& item, std::size_t total,
                             std::size_t current) {
  switch (progress) {
    case ALPM_PROGRESS_KEYRING_START:
    case ALPM_PROGRESS_INTEGRITY_START:
    case ALPM_PROGRESS_LOAD_START:
    case ALPM_PROGRESS_CONFLICTS_START:
    case ALPM_PROGRESS_DISKSPACE_START:
      emit(ProgressPhase::Verifying, clamp(current > 0 ? current - 1 : 0), clamp(total), item);
      break;
    default:
      break;  // install/upgrade/remove progress is reported through the package operation events
  }
}

void EventMapper::finish() {
  if (!sink_ || finished_) {
    return;
  }
  sink_(holonight_packages_domain::ProgressUpdate{.phase = ProgressPhase::Done, .current = 1, .total = 1, .item = {}});
  finished_ = true;
}

}  // namespace holonight_packages_transaction
