#include "holonight_packages_persistence/cache_locations.h"

#include <cstdlib>

namespace holonight_packages_persistence {

namespace {

std::optional<std::string> nonEmpty(const EnvLookup& env, std::string_view name) {
  auto value = env(name);
  if (!value.has_value() || value->empty()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::optional<std::filesystem::path> appCacheDir(const EnvLookup& env) {
  if (const auto xdg = nonEmpty(env, "XDG_CACHE_HOME")) {
    return std::filesystem::path(*xdg) / "holonight-packages";
  }
  if (const auto home = nonEmpty(env, "HOME")) {
    return std::filesystem::path(*home) / ".cache" / "holonight-packages";
  }
  return std::nullopt;
}

EnvLookup processEnvironment() {
  return [](std::string_view name) -> std::optional<std::string> {
    const std::string key(name);
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once at startup, before threads write the environment.
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
      return std::nullopt;
    }
    return std::string(value);
  };
}

}  // namespace holonight_packages_persistence
