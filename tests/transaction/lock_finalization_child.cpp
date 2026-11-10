#include "alpm_session.h"
#include "fixture_root.h"
#include "holonight_packages_transaction/database_lock.h"

#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <unistd.h>

using namespace holonight_packages_transaction;
using holonight_packages_domain::PlanAction;

namespace {
void checkpoint(const char* message) {
  std::cout << message << '\n' << std::flush;
  std::string command;
  if (!std::getline(std::cin, command)) {
    std::_Exit(10);
  }
}
}  // namespace

// Dedicated subprocess: no production switches, and every path belongs to the parent's temporary fixture.
int main(int argc, char* argv[]) {
  ::alarm(8);
  if (argc != 3) {
    return 2;
  }
  const std::span arguments(argv, static_cast<std::size_t>(argc));
  const std::string mode(arguments[1]);
  const std::filesystem::path base(arguments[2]);
  if (holonight_transaction_testing::FixtureRoot::rejectRoot(base, std::filesystem::temp_directory_path())) {
    return 3;
  }
  const TransactionConfig config{
      .root = base / "root",
      .database_path = base / "db",
      .pacman_conf_path = base / "pacman.conf",
      .cache_directory = base / "cache",
  };
  auto opened = AlpmSession::open(config, PlanAction::Remove, {});
  if (!opened) {
    return 4;
  }
  auto session = std::move(*opened);
  if (mode == "foreign") {
    if (session->beginTransaction(0)) {
      return 5;
    }
  } else if (mode == "acquired-worker") {
    bool acquired = false;
    std::thread worker([&] { acquired = session->beginTransaction(0); });
    worker.join();
    if (!acquired) {
      return 6;
    }
  } else if (mode != "init-blocked" && !session->beginTransaction(0)) {
    return 6;
  }
  checkpoint("ready");
  if (mode == "released") {
    std::thread worker([&] { session->releaseTransaction(); });
    worker.join();
    checkpoint("released");
  }
  const DatabaseLockGuard guard;
  const bool expectedOwner = mode != "foreign" && mode != "released" && mode != "init-blocked";
  if (guard.ownsLock() != expectedOwner) {
    return 7;
  }
  if (guard.ownsLock()) {
    std::error_code ignored;
    std::filesystem::remove(base / "db/db.lck", ignored);
  }
  if (mode == "release-blocked" || mode == "teardown-blocked" || mode == "init-blocked") {
    std::promise<void> attempting;
    auto attempted = attempting.get_future();
    std::thread worker([&] {
      attempting.set_value();
      if (mode == "release-blocked") {
        session->releaseTransaction();
      } else if (mode == "teardown-blocked") {
        session.reset();  // handle teardown must be serialized too
      } else {
        (void)session->beginTransaction(0);
      }
      std::_Exit(9);  // A worker lifecycle operation must never finish while finalization holds the guard.
    });
    attempted.wait();
    checkpoint("blocked");
    std::_Exit(0);  // Keep both guard and worker alive until process exit.
  }
  checkpoint("finalized");
  std::_Exit(0);
}
