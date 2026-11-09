#pragma once

#include <string_view>

namespace holonight_packages_transaction {

// Orders two package versions with libalpm's own comparison: negative when `left` is older, zero when equal,
// positive when newer. This is the only version comparison in the transaction library (REQ-C-004).
[[nodiscard]] int packageVersionOrder(std::string_view left, std::string_view right);

}  // namespace holonight_packages_transaction
