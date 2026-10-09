#include "pending_update_computation.h"

#include "holonight_packages_backends/alpm_update_source.h"

#include <algorithm>
#include <alpm.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

namespace holonight_packages_backends {
namespace {

std::filesystem::path updatesFixture() {
  return std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "updates";
}

TEST(PendingUpdateComputation, MatchesLoadUpdatesOnTheSameDatabases) {
  const auto fixture = updatesFixture();
  const AlpmUpdateSource source(AlpmUpdateSourceOptions{
      .databaseRoot = fixture,
      .databasePath = fixture,
      .pacmanConfPath = fixture / "pacman.conf",
  });
  const auto expected = source.loadUpdates();
  ASSERT_TRUE(expected.has_value());

  // Open the same databases directly, registering them in alphabetical file order like the connection cache.
  alpm_errno_t error = ALPM_ERR_OK;
  const std::unique_ptr<alpm_handle_t, int (*)(alpm_handle_t*)> handle(
      alpm_initialize(fixture.c_str(), fixture.c_str(), &error), &alpm_release);
  ASSERT_NE(handle, nullptr);
  std::vector<alpm_db_t*> syncDatabases;
  for (const char* name : {"core", "extra"}) {
    alpm_db_t* database = alpm_register_syncdb(handle.get(), name, 0);
    ASSERT_NE(database, nullptr);
    syncDatabases.push_back(database);
  }
  const auto config = parsePacmanConfig(fixture / "pacman.conf");
  ASSERT_TRUE(config.has_value());

  const auto computed =
      computePendingUpdates(alpm_db_get_pkgcache(alpm_get_localdb(handle.get())), syncDatabases, *config);

  auto sortedByName = [](std::vector<holonight_packages_domain::PendingUpdate> rows) {
    std::ranges::sort(rows, {}, &holonight_packages_domain::PendingUpdate::name);
    return rows;
  };
  EXPECT_EQ(sortedByName(computed), sortedByName(expected->updates));
  EXPECT_EQ(computed.size(), 5U);
}

}  // namespace
}  // namespace holonight_packages_backends
