#include "holonight_packages_application/update_age_format.h"

namespace holonight_packages_application {

std::string formatSnapshotAge(std::chrono::system_clock::time_point now,
                              std::chrono::system_clock::time_point fetchedAt) {
  using std::chrono::duration_cast;
  using std::chrono::hours;
  using std::chrono::minutes;
  const auto age = now - fetchedAt;
  if (age < minutes{1}) {
    return "just now";
  }
  if (age < hours{1}) {
    return std::to_string(duration_cast<minutes>(age).count()) + " min";
  }
  if (age < hours{48}) {
    return std::to_string(duration_cast<hours>(age).count()) + " h";
  }
  return std::to_string(duration_cast<std::chrono::days>(age).count()) + " d";
}

}  // namespace holonight_packages_application
