#include "holonight_packages_domain/transaction_request.h"

#include <algorithm>
#include <set>

namespace holonight_packages_domain {
namespace {

bool isGrammarCharacter(char character) noexcept {
  return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '@' ||
         character == '.' || character == '_' || character == '+' || character == '-';
}

TransactionFailure invalid(std::vector<std::string> offending) {
  return TransactionFailure{
      .code = TransactionErrorCode::InvalidArgument,
      .names = std::move(offending),
      .choices = {},
  };
}

}  // namespace

std::string_view planActionName(PlanAction action) noexcept {
  switch (action) {
    case PlanAction::Install:
      return "install";
    case PlanAction::Remove:
      return "remove";
    case PlanAction::Upgrade:
      return "upgrade";
  }
  return "install";
}

bool matchesPackageNameGrammar(std::string_view name) noexcept {
  if (name.empty() || name.size() > kMaxPackageNameLength || name.front() == '-' || name.front() == '.') {
    return false;
  }
  return std::ranges::all_of(name, isGrammarCharacter);
}

bool isValidPackageName(std::string_view name) noexcept {
  return matchesPackageNameGrammar(name) && !name.contains('/') && !name.contains(".pkg.tar") && !name.contains("://");
}

std::string sanitizeOffendingValue(std::string_view value) {
  std::string result;
  result.reserve(std::min(value.size(), kMaxOffendingValueBytes));
  for (const char character : value) {
    if (result.size() >= kMaxOffendingValueBytes) {
      break;
    }
    const bool control = static_cast<unsigned char>(character) < 0x20 || character == 0x7F;
    result.push_back(control ? '?' : character);
  }
  return result;
}

std::expected<PlanRequest, TransactionFailure> validateUpgradeRequest(const std::vector<std::string>& optionKeys) {
  if (!optionKeys.empty()) {
    std::vector<std::string> keys;
    keys.reserve(optionKeys.size());
    for (const std::string& key : optionKeys) {
      keys.push_back(sanitizeOffendingValue(key));
    }
    return std::unexpected(invalid(std::move(keys)));
  }
  return PlanRequest{.action = PlanAction::Upgrade, .names = {}};
}

std::expected<PlanRequest, TransactionFailure> validateRequest(std::string_view action,
                                                               const std::vector<std::string>& names,
                                                               const std::vector<std::string>& optionKeys) {
  PlanRequest request;
  if (action == "install") {
    request.action = PlanAction::Install;
  } else if (action == "remove") {
    request.action = PlanAction::Remove;
  } else {
    return std::unexpected(invalid({sanitizeOffendingValue(action)}));
  }
  if (!optionKeys.empty()) {
    std::vector<std::string> keys;
    keys.reserve(optionKeys.size());
    for (const std::string& key : optionKeys) {
      keys.push_back(sanitizeOffendingValue(key));
    }
    return std::unexpected(invalid(std::move(keys)));
  }
  if (names.empty() || names.size() > kMaxRequestNames) {
    return std::unexpected(invalid({}));
  }
  std::vector<std::string> offending;
  std::set<std::string_view> seen;
  for (const std::string& name : names) {
    if (!isValidPackageName(name) || !seen.insert(name).second) {
      offending.push_back(sanitizeOffendingValue(name));
    }
  }
  if (!offending.empty()) {
    return std::unexpected(invalid(std::move(offending)));
  }
  request.names = names;
  return request;
}

}  // namespace holonight_packages_domain
