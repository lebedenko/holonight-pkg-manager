#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace holonight_packages_domain {

template <typename T>
[[nodiscard]] std::shared_ptr<T> requireNonNull(std::shared_ptr<T> pointer, std::string_view context) {
  if (!pointer) {
    throw std::invalid_argument(std::string(context));
  }
  return pointer;
}

}  // namespace holonight_packages_domain
