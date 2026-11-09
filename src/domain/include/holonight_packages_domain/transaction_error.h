#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_domain {

// Every refusal and failure of the package helper. The wire name is `transactionErrorName()`; nothing else about a
// failure (no libalpm text) ever reaches a caller (REQ-F-051, REQ-F-052).
enum class TransactionErrorCode : std::uint8_t {
  InvalidArgument,
  PackageNotFound,
  PackageIgnored,
  ProtectedPackage,
  BreaksDependents,
  NeedsDecision,
  PartialUpgradeRequired,
  InsecureSigLevel,
  SyncDatabaseStale,
  DownloadFailed,
  FileConflicts,
  DiskSpace,
  SignatureInvalid,
  DatabaseUnavailable,
  ConfigurationError,
  NothingToDo,
  PlanNotFound,
  PlanExpired,
  PlanChanged,
  NotAuthorized,
  Busy,
  PastPointOfNoReturn,
  TransactionFailed,
  InternalError,
};

inline constexpr std::size_t kTransactionErrorCodeCount = 24;

// A decision that resolution cannot make for the caller (REQ-F-024).
struct NeedsDecisionChoice {
  enum class Kind : std::uint8_t { Provider, Conflict, Replace };
  Kind kind = Kind::Provider;
  std::string subject;
  std::vector<std::string> candidates;

  bool operator==(const NeedsDecisionChoice&) const = default;
};

[[nodiscard]] std::string_view needsDecisionKindName(NeedsDecisionChoice::Kind kind) noexcept;

// A refusal or failure: one code, the package or repository names it refers to, and for NeedsDecision the choices.
struct TransactionFailure {
  TransactionErrorCode code = TransactionErrorCode::InternalError;
  std::vector<std::string> names;
  std::vector<NeedsDecisionChoice> choices;

  bool operator==(const TransactionFailure&) const = default;
};

[[nodiscard]] std::string_view transactionErrorName(TransactionErrorCode code) noexcept;
[[nodiscard]] std::optional<TransactionErrorCode> parseTransactionErrorCode(std::string_view name) noexcept;

// The refusal order of REQ-F-030, stated once. Stage one runs before the libalpm prepare step and stage two after it.
// DatabaseUnavailable is an environment error and sits outside the order.
[[nodiscard]] std::span<const TransactionErrorCode> stageOneRefusalOrder() noexcept;
[[nodiscard]] std::span<const TransactionErrorCode> stageTwoRefusalOrder() noexcept;

}  // namespace holonight_packages_domain
