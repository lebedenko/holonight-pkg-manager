#pragma once

#include "holonight_packages_application/update_check_ports.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <random>

namespace holonight_packages_testing {

// Deterministic generator with a fixed seed.
class SeededRandom final : public holonight_packages_application::RandomSource {
 public:
  explicit SeededRandom(std::uint32_t seed = 42) : engine_(seed) {}

  [[nodiscard]] std::int64_t uniform(std::int64_t low, std::int64_t high) override {
    return std::uniform_int_distribution<std::int64_t>(low, high)(engine_);
  }

 private:
  std::mt19937_64 engine_;
};

// Returns queued values (clamped into the requested range), then the lower bound.
class ScriptedRandom final : public holonight_packages_application::RandomSource {
 public:
  void enqueue(std::int64_t value) { values_.push_back(value); }

  [[nodiscard]] std::int64_t uniform(std::int64_t low, std::int64_t high) override {
    if (values_.empty()) {
      return low;
    }
    const std::int64_t value = values_.front();
    values_.pop_front();
    return std::clamp(value, low, high);
  }

 private:
  std::deque<std::int64_t> values_;
};

}  // namespace holonight_packages_testing
