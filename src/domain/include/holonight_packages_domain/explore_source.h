#pragma once

#include "holonight_packages_domain/sync_package.h"

#include <cstdint>
#include <expected>
#include <string>

namespace holonight_packages_domain {

enum class ExploreSourceErrorCode : std::uint8_t { DatabaseOpenFailed, Unknown };

struct ExploreSourceError {
  ExploreSourceErrorCode code;
  // User-presentable reason.
  std::string message;
};

class ExploreSource {
 public:
  ExploreSource() = default;
  ExploreSource(const ExploreSource&) = default;
  ExploreSource(ExploreSource&&) = default;
  ExploreSource& operator=(const ExploreSource&) = default;
  ExploreSource& operator=(ExploreSource&&) = default;
  virtual ~ExploreSource();

  // Read-only enumeration of every package in the current sync databases. Makes no network access and never writes.
  // Blocking; intended to run on a worker thread. Serves both the initial load and Reload.
  [[nodiscard]] virtual std::expected<ExploreSnapshot, ExploreSourceError> loadPackages() const = 0;
};

}  // namespace holonight_packages_domain
