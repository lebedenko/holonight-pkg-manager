#include "history_tag.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

namespace holonight_packages_transaction {

using holonight_packages_domain::PlanAction;

namespace {

std::string_view actionWord(PlanAction action) noexcept {
  switch (action) {
    case PlanAction::Install:
      return "install";
    case PlanAction::Remove:
      return "remove";
    case PlanAction::Upgrade:
      break;
  }
  return "upgrade";
}

}  // namespace

std::optional<std::string> formatHistoryTag(PlanAction action, std::span<const std::string> names, std::uint32_t uid) {
  if ((action == PlanAction::Upgrade) != names.empty()) {
    return std::nullopt;
  }
  if (!std::ranges::all_of(
          names, [](const std::string& name) { return holonight_packages_domain::isValidPackageName(name); })) {
    return std::nullopt;
  }
  std::string command(actionWord(action));
  for (const std::string& name : names) {
    command += ' ';
    command += name;
  }
  return std::format("Running '{}' (uid {})", command, uid);
}

HistoryTagWriter::HistoryTagWriter(const TransactionSeams& seams, LogSink journal)
    : seams_(&seams), journal_(std::move(journal)) {}

TagOutcome HistoryTagWriter::write(alpm_handle_t* handle, PlanAction action, std::span<const std::string> names,
                                   std::optional<std::uint32_t> caller_uid) const {
  if (!caller_uid) {
    return TagOutcome::Skipped;
  }
  const std::optional<std::string> text = formatHistoryTag(action, names, *caller_uid);
  if (!text) {
    return TagOutcome::Skipped;
  }
  const auto observe = [this](std::string_view stage) {
    if (seams_->observe) {
      seams_->observe(stage);
    }
  };
  observe("tag-writing");
  const int fault = seams_->inject_fault ? seams_->inject_fault("tag-write") : 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): libalpm's logging entry point is printf-style.
  const int status = fault != 0 ? -1 : alpm_logaction(handle, "HOLONIGHT", "%s\n", text->c_str());
  if (status != 0) {
    observe("tag-failed");
    if (journal_) {
      journal_(LogLevel::Warning, "the history tag was not written to the pacman log; the transaction continues");
    }
    return TagOutcome::Failed;
  }
  observe("tag-written");
  return TagOutcome::Written;
}

}  // namespace holonight_packages_transaction
