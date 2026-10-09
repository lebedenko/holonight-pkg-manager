#include "holonight_packages_domain/update_checker.h"

namespace holonight_packages_domain {

namespace {

struct ErrorTableRow {
  UpdateCheckErrorCode code;
  std::string_view name;
  std::string_view message;
};

constexpr std::array<ErrorTableRow, 4> kErrorTable{
    {
        {
            .code = UpdateCheckErrorCode::NetworkUnavailable,
            .name = "network-unavailable",
            .message = "No network connection to the package servers.",
        },
        {
            .code = UpdateCheckErrorCode::RepositoryUnreachable,
            .name = "repository-unreachable",
            .message = "A package repository could not be reached.",
        },
        {
            .code = UpdateCheckErrorCode::Busy,
            .name = "busy",
            .message = "Another package operation is using the package database.",
        },
        {
            .code = UpdateCheckErrorCode::Unknown,
            .name = "unknown",
            .message = "The check failed for an unexpected reason.",
        },
    },
};

const ErrorTableRow& rowFor(UpdateCheckErrorCode code) noexcept {
  for (const ErrorTableRow& row : kErrorTable) {
    if (row.code == code) {
      return row;
    }
  }
  return kErrorTable.back();
}

}  // namespace

std::string_view updateCheckErrorMessage(UpdateCheckErrorCode code) noexcept { return rowFor(code).message; }

std::string_view updateCheckErrorName(UpdateCheckErrorCode code) noexcept { return rowFor(code).name; }

UpdateChecker::~UpdateChecker() = default;

}  // namespace holonight_packages_domain
