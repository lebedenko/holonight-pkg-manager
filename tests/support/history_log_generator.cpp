#include "history_log_generator.h"

#include <array>
#include <chrono>
#include <format>
#include <string_view>
#include <vector>

namespace holonight_test {

namespace {

using namespace std::chrono_literals;

constexpr std::array kWords = {
    "lib",    "python", "qt6",  "gtk",   "linux", "firmware", "mesa",  "vulkan", "font",   "perl",
    "rust",   "node",   "kde",  "gnome", "xorg",  "wayland",  "audio", "pipe",   "codec",  "media",
    "crypto", "ssl",    "core", "utils", "tools", "headers",  "docs",  "devel",  "plugin", "theme",
};
constexpr std::array kHooks = {
    "20-systemd-sysusers.hook",   "30-systemd-update.hook",     "60-mkinitcpio-remove.hook",
    "90-mkinitcpio-install.hook", "gtk-update-icon-cache.hook", "update-desktop-database.hook",
    "texinfo-install.hook",       "dbus-reload.hook",           "fontconfig.hook",
};
constexpr std::string_view kScriptletText =
    "==> Building image from preset: /etc/mkinitcpio.d/linux.preset: 'default' -> -k /boot/vmlinuz-linux "
    "-g /boot/initramfs-linux.img";

std::size_t below(std::mt19937_64& random, std::size_t bound) {
  return std::uniform_int_distribution<std::size_t>(0, bound - 1)(random);
}

std::string packageName(std::mt19937_64& random) {
  std::string name = kWords.at(below(random, kWords.size()));
  name += '-';
  name += kWords.at(below(random, kWords.size()));
  name += std::to_string(below(random, 100));
  return name;
}

std::string version(std::mt19937_64& random) {
  return std::format("{}.{}.{}-{}", below(random, 30), below(random, 20), below(random, 100), 1 + below(random, 3));
}

// Lines of one transaction without scriptlet output; `scriptlets` lines of it are added after the actions.
class TransactionWriter {
 public:
  TransactionWriter(std::mt19937_64& random, std::chrono::sys_seconds start) : random_(&random), time_(start) {}

  void write(std::string& out, std::size_t& lines, std::size_t actions, std::size_t pre_hooks, std::size_t post_hooks,
             std::size_t scriptlets) {
    const std::size_t origin = below(*random_, 10);
    if (origin < 7) {
      line(out, lines, "[PACMAN] Running 'pacman --sync --refresh --sysupgrade'");
    } else if (origin < 9) {
      line(out, lines, "[HOLONIGHT] Running 'install " + packageName(*random_) + "' (uid 1000)");
    }
    for (std::size_t i = 0; i < pre_hooks; ++i) {
      hook(out, lines);
    }
    line(out, lines, "[ALPM] transaction started");
    for (std::size_t i = 0; i < actions; ++i) {
      line(out, lines, action());
    }
    for (std::size_t i = 0; i < scriptlets; ++i) {
      const std::size_t length = 20 + below(*random_, kScriptletText.size() - 20);
      line(out, lines, "[ALPM-SCRIPTLET] " + std::string(kScriptletText.substr(0, length)));
    }
    line(out, lines, "[ALPM] transaction completed");
    for (std::size_t i = 0; i < post_hooks; ++i) {
      hook(out, lines);
    }
  }

  [[nodiscard]] std::chrono::sys_seconds time() const { return time_; }

 private:
  std::string action() {
    const std::string name = packageName(*random_);
    switch (below(*random_, 20)) {
      case 0:
      case 1:
        return std::format("[ALPM] installed {} ({})", name, version(*random_));
      case 2:
        return std::format("[ALPM] removed {} ({})", name, version(*random_));
      case 3:
        return std::format("[ALPM] downgraded {} ({} -> {})", name, version(*random_), version(*random_));
      case 4:
        return std::format("[ALPM] reinstalled {} ({})", name, version(*random_));
      default:
        return std::format("[ALPM] upgraded {} ({} -> {})", name, version(*random_), version(*random_));
    }
  }

  void hook(std::string& out, std::size_t& lines) {
    line(out, lines, std::format("[ALPM] running '{}'...", kHooks.at(below(*random_, kHooks.size()))));
  }

  void line(std::string& out, std::size_t& lines, std::string_view text) {
    time_ += std::chrono::seconds(below(*random_, 3));
    out += std::format("[{:%FT%T}+0000] {}\n", time_, text);
    ++lines;
  }

  std::mt19937_64* random_;
  std::chrono::sys_seconds time_;
};

struct Plan {
  std::size_t actions;
  std::size_t pre_hooks;
  std::size_t post_hooks;
  std::size_t scriptlets = 0;
};

}  // namespace

GeneratedHistoryLog generateHistoryLog(const HistoryLogShape& shape) {
  std::mt19937_64 random(shape.seed);
  std::vector<Plan> plans;
  plans.reserve(shape.transactions);
  // Running line (at most one), start and completed lines, actions and hooks.
  std::size_t structural = 0;
  for (std::size_t i = 0; i < shape.transactions; ++i) {
    plans.push_back(
        Plan{.actions = 1 + below(random, 40), .pre_hooks = below(random, 3), .post_hooks = below(random, 7)});
    structural += 3 + plans.back().actions + plans.back().pre_hooks + plans.back().post_hooks;
  }
  // The rest is scriptlet output, spread over the transactions at random.
  const std::size_t scriptlets = shape.lines > structural ? shape.lines - structural : 0;
  for (std::size_t i = 0; i < scriptlets; ++i) {
    ++plans.at(below(random, plans.size())).scriptlets;
  }

  GeneratedHistoryLog log;
  log.text.reserve(6U << 20U);
  std::chrono::sys_seconds time = std::chrono::sys_days{std::chrono::year{2019} / 1 / 1};
  for (const Plan& plan : plans) {
    TransactionWriter writer(random, time);
    writer.write(log.text, log.lines, plan.actions, plan.pre_hooks, plan.post_hooks, plan.scriptlets);
    time = writer.time() + std::chrono::minutes(30 + below(random, 3000));
    ++log.transactions;
  }
  // A Running line is optional, so the count can fall short; pad the last transaction's tail with scriptlet lines.
  while (log.lines < shape.lines) {
    log.text += std::format("[{:%FT%T}+0000] [ALPM-SCRIPTLET] {}\n", time, kScriptletText.substr(0, 40));
    ++log.lines;
  }
  return log;
}

std::string generatedTransaction(std::mt19937_64& random, std::size_t index) {
  const std::chrono::sys_seconds start =
      std::chrono::sys_days{std::chrono::year{2030} / 1 / 1} + std::chrono::hours(index);
  std::string text;
  std::size_t lines = 0;
  TransactionWriter(random, start).write(text, lines, 8, 1, 2, 2);
  return text;
}

}  // namespace holonight_test
