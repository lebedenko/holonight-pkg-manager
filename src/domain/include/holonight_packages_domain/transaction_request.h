#pragma once

#include "holonight_packages_domain/transaction_error.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_domain {

enum class PlanAction : std::uint8_t { Install, Remove };

[[nodiscard]] std::string_view planActionName(PlanAction action) noexcept;

// What a caller may ask for: an action and package names. Nothing else (REQ-F-016, REQ-F-017).
struct PlanRequest {
  PlanAction action = PlanAction::Install;
  std::vector<std::string> names;

  bool operator==(const PlanRequest&) const = default;
};

inline constexpr std::size_t kMaxRequestNames = 64;
inline constexpr std::size_t kMaxPackageNameLength = 255;
inline constexpr std::size_t kMaxOffendingValueBytes = 256;

// REQ-F-013: characters from [a-z0-9@._+-], not beginning with '-' or '.', 1 to 255 characters.
[[nodiscard]] bool matchesPackageNameGrammar(std::string_view name) noexcept;

// REQ-F-013 and REQ-F-014: the grammar, and additionally no path separator, no ".pkg.tar" and no URL scheme, so a name
// can never be read as a file or a URL.
[[nodiscard]] bool isValidPackageName(std::string_view name) noexcept;

// An offending value as it may be echoed to the caller: at most 256 bytes, control characters replaced (REQ-F-014).
[[nodiscard]] std::string sanitizeOffendingValue(std::string_view value);

// Validates what a caller sent to PreparePlan before libalpm is touched: the action, 1 to 64 unique valid names, and no
// options (REQ-F-014..016). A failure is InvalidArgument whose `names` hold the sanitized offending values.
[[nodiscard]] std::expected<PlanRequest, TransactionFailure> validateRequest(
    std::string_view action, const std::vector<std::string>& names, const std::vector<std::string>& optionKeys);

}  // namespace holonight_packages_domain
