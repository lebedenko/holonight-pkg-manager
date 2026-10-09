#include "PackagedConfig.h"

#include "holonight/config/document.h"

#include <cstdlib>
#include <variant>

namespace packaged_config {

namespace {

std::optional<std::string> nonEmpty(const EnvLookup& env, std::string_view name) {
  auto value = env(name);
  if (!value.has_value() || value->empty()) {
    return std::nullopt;
  }
  return value;
}

// Renders whatever the file holds in a form resolveCheckInterval can judge; non-integers become unusable text.
std::string rawText(const HoloNight::Config::Value& value) {
  if (const auto* number = std::get_if<std::int64_t>(&value)) {
    return std::to_string(*number);
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  return "not-a-number";
}

}  // namespace

std::optional<std::filesystem::path> configPath(const EnvLookup& env) {
  if (const auto file = nonEmpty(env, "HOLONIGHT_PACKAGES_FILE")) {
    return std::filesystem::path(*file);
  }
  if (const auto xdg = nonEmpty(env, "XDG_CONFIG_HOME")) {
    return std::filesystem::path(*xdg) / "holonight" / "packages.toml";
  }
  if (const auto home = nonEmpty(env, "HOME")) {
    return std::filesystem::path(*home) / ".config" / "holonight" / "packages.toml";
  }
  return std::nullopt;
}

EnvLookup processEnvironment() {
  return [](std::string_view name) -> std::optional<std::string> {
    const std::string key(name);
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once at startup.
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
      return std::nullopt;
    }
    return std::string(value);
  };
}

Settings load(const std::optional<std::filesystem::path>& path,
              const std::optional<std::string>& cliCheckIntervalMinutes,
              const holonight_packages_application::UpdateCheckPolicy& policy) {
  Settings settings{};
  std::optional<std::string> raw = cliCheckIntervalMinutes;
  if (!raw.has_value() && path.has_value()) {
    const auto document = HoloNight::Config::readDocument(*path);
    if (!document) {
      settings.warnings.push_back("Cannot read " + path->string() + "; using the default check interval.");
    } else if (const auto value = document.value->value({"updates", "check_interval_minutes"})) {
      raw = rawText(*value);
    }
  }
  std::optional<std::string_view> view;
  if (raw.has_value()) {
    view = *raw;
  }
  auto resolved = holonight_packages_application::resolveCheckInterval(view, policy);
  settings.checkInterval = resolved.interval;
  if (resolved.warning.has_value()) {
    settings.warnings.push_back(std::move(*resolved.warning));
  }
  return settings;
}

}  // namespace packaged_config
