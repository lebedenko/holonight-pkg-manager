#pragma once

#include "holonight_packages_domain/pending_update.h"

#include <cstdint>
#include <expected>
#include <string>

namespace holonight_packages_domain {

enum class UpdateSourceErrorCode : std::uint8_t { ConfigurationInvalid, DatabaseOpenFailed, Unknown };

struct UpdateSourceError {
  UpdateSourceErrorCode code;
  // User-presentable reason.
  std::string message;
};

class UpdateSource {
 public:
  UpdateSource() = default;
  UpdateSource(const UpdateSource&) = default;
  UpdateSource(UpdateSource&&) = default;
  UpdateSource& operator=(const UpdateSource&) = default;
  UpdateSource& operator=(UpdateSource&&) = default;
  virtual ~UpdateSource();

  // Read-only comparison of installed packages against the current sync databases. Makes no network access and
  // never writes. Blocking; intended to run on a worker thread. Serves both the initial load and Reload.
  [[nodiscard]] virtual std::expected<UpdateSnapshot, UpdateSourceError> loadUpdates() const = 0;
};

}  // namespace holonight_packages_domain
