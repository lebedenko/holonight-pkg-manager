#include "holonight_packages_domain/transaction_error.h"

#include <utility>

namespace holonight_packages_domain {
namespace {

using Code = TransactionErrorCode;

constexpr std::array<std::pair<Code, std::string_view>, kTransactionErrorCodeCount> kNames{
    {
        {Code::InvalidArgument, "InvalidArgument"},
        {Code::PackageNotFound, "PackageNotFound"},
        {Code::PackageIgnored, "PackageIgnored"},
        {Code::ProtectedPackage, "ProtectedPackage"},
        {Code::BreaksDependents, "BreaksDependents"},
        {Code::NeedsDecision, "NeedsDecision"},
        {Code::PartialUpgradeRequired, "PartialUpgradeRequired"},
        {Code::InsecureSigLevel, "InsecureSigLevel"},
        {Code::SyncDatabaseStale, "SyncDatabaseStale"},
        {Code::DownloadFailed, "DownloadFailed"},
        {Code::FileConflicts, "FileConflicts"},
        {Code::DiskSpace, "DiskSpace"},
        {Code::SignatureInvalid, "SignatureInvalid"},
        {Code::DatabaseUnavailable, "DatabaseUnavailable"},
        {Code::ConfigurationError, "ConfigurationError"},
        {Code::NothingToDo, "NothingToDo"},
        {Code::PlanNotFound, "PlanNotFound"},
        {Code::PlanExpired, "PlanExpired"},
        {Code::PlanChanged, "PlanChanged"},
        {Code::NotAuthorized, "NotAuthorized"},
        {Code::Busy, "Busy"},
        {Code::PastPointOfNoReturn, "PastPointOfNoReturn"},
        {Code::TransactionFailed, "TransactionFailed"},
        {Code::InternalError, "InternalError"},
        {Code::SyncFailed, "SyncFailed"},
        {Code::SyncDatabaseChanged, "SyncDatabaseChanged"},
        {Code::KeyImportRequired, "KeyImportRequired"},
        {Code::UpgradePartiallyApplied, "UpgradePartiallyApplied"},
        {Code::Cancelled, "Cancelled"},
    },
};

// REQ-F-030, stage one: before libalpm's prepare step.
constexpr std::array<Code, 8> kStageOne{
    Code::InvalidArgument,  Code::ConfigurationError, Code::PackageNotFound,  Code::PackageIgnored,
    Code::InsecureSigLevel, Code::ProtectedPackage,   Code::BreaksDependents, Code::NothingToDo,
};
// Stage two: after libalpm's prepare step, for the dependencies the prepare step resolved.
constexpr std::array<Code, 5> kStageTwo{
    Code::PackageNotFound, Code::NeedsDecision, Code::ProtectedPackage, Code::PartialUpgradeRequired, Code::NothingToDo,
};

}  // namespace

std::string_view transactionErrorName(TransactionErrorCode code) noexcept {
  for (const auto& [candidate, name] : kNames) {
    if (candidate == code) {
      return name;
    }
  }
  return "InternalError";
}

std::optional<TransactionErrorCode> parseTransactionErrorCode(std::string_view name) noexcept {
  for (const auto& [candidate, text] : kNames) {
    if (text == name) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::string_view needsDecisionKindName(NeedsDecisionChoice::Kind kind) noexcept {
  switch (kind) {
    case NeedsDecisionChoice::Kind::Provider:
      return "provider";
    case NeedsDecisionChoice::Kind::Conflict:
      return "conflict";
    case NeedsDecisionChoice::Kind::Replace:
      return "replace";
  }
  return "provider";
}

std::span<const TransactionErrorCode> stageOneRefusalOrder() noexcept { return kStageOne; }
std::span<const TransactionErrorCode> stageTwoRefusalOrder() noexcept { return kStageTwo; }

}  // namespace holonight_packages_domain
