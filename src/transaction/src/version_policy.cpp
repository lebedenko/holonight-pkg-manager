#include "version_policy.h"

#include <alpm.h>
#include <string>

namespace holonight_packages_transaction {

int packageVersionOrder(std::string_view left, std::string_view right) {
  const std::string left_copy(left);
  const std::string right_copy(right);
  return alpm_pkg_vercmp(left_copy.c_str(), right_copy.c_str());
}

}  // namespace holonight_packages_transaction
