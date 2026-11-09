#include "gpg_keyring.h"

#include "fixture_root.h"
#include "gpgme_guard.h"
#include "repo_builder.h"

#include <alpm.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_transaction_testing {
namespace {

namespace fs = std::filesystem;

TEST(GpgKeyringTest, GeneratesAKeyQuicklyInsideTheWorldAndSignsFiles) {
  const FixtureRoot world;
  const auto started = std::chrono::steady_clock::now();
  const GpgKeyring keyring(world.base() / "signing");
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
  EXPECT_EQ(keyring.fingerprint().size(), 40U);

  const fs::path file = world.base() / "payload.bin";
  std::ofstream(file) << "payload";
  keyring.signFile(file);
  EXPECT_TRUE(fs::is_regular_file(world.base() / "payload.bin.sig"));
}

TEST(GpgKeyringTest, TwoKeyringsHaveDifferentKeys) {
  const FixtureRoot world;
  const GpgKeyring first(world.base() / "first");
  const GpgKeyring second(world.base() / "second");
  EXPECT_NE(first.fingerprint(), second.fingerprint());
}

TEST(GpgKeyringTest, SignedPackagesVerifyUnderRequiredAndUnsignedOnesDoNot) {
  SKIP_UNLESS_FRESH_GPGME();
  const FixtureRoot world;
  const GpgKeyring keyring(world.base() / "signing");
  keyring.trustIn(world.gpgDir());
  RepoBuilder repo(world, "core");
  repo.setSigner(keyring.signer());
  repo.add(PackageSpec{.name = "signed", .files = {{"usr/signed", "s"}}});
  repo.setSigner(nullptr);
  repo.add(PackageSpec{.name = "plain", .files = {{"usr/plain", "p"}}});
  repo.publish();
  EXPECT_TRUE(fs::exists(repo.packagePath("signed").string() + ".sig"));
  EXPECT_FALSE(fs::exists(repo.packagePath("plain").string() + ".sig"));

  alpm_errno_t error{};
  alpm_handle_t* handle = alpm_initialize(world.installRoot().c_str(), world.dbPath().c_str(), &error);
  ASSERT_NE(handle, nullptr);
  alpm_option_set_gpgdir(handle, world.gpgDir().c_str());
  alpm_option_set_default_siglevel(handle, ALPM_SIG_PACKAGE);
  alpm_option_set_disable_sandbox_filesystem(handle, 1);
  alpm_option_set_disable_sandbox_syscalls(handle, 1);

  alpm_pkg_t* signedPackage = nullptr;
  EXPECT_EQ(alpm_pkg_load(handle, repo.packagePath("signed").c_str(), 1, ALPM_SIG_PACKAGE, &signedPackage), 0)
      << alpm_strerror(alpm_errno(handle));
  EXPECT_NE(signedPackage, nullptr);
  if (signedPackage != nullptr) {
    alpm_pkg_free(signedPackage);
  }
  alpm_pkg_t* plainPackage = nullptr;
  EXPECT_NE(alpm_pkg_load(handle, repo.packagePath("plain").c_str(), 1, ALPM_SIG_PACKAGE, &plainPackage), 0);
  EXPECT_EQ(plainPackage, nullptr);
  alpm_release(handle);
}

TEST(GpgKeyringTest, AKeyWithoutOwnerTrustDoesNotVerifyForTrustedOnly) {
  SKIP_UNLESS_FRESH_GPGME();
  const FixtureRoot world;
  const GpgKeyring keyring(world.base() / "signing");
  keyring.trustIn(world.gpgDir(), GpgKeyring::Trust::None);
  RepoBuilder repo(world, "core");
  repo.setSigner(keyring.signer());
  repo.add(PackageSpec{.name = "signed", .files = {{"usr/signed", "s"}}});

  alpm_errno_t error{};
  alpm_handle_t* handle = alpm_initialize(world.installRoot().c_str(), world.dbPath().c_str(), &error);
  ASSERT_NE(handle, nullptr);
  alpm_option_set_gpgdir(handle, world.gpgDir().c_str());
  alpm_pkg_t* package = nullptr;
  EXPECT_NE(alpm_pkg_load(handle, repo.packagePath("signed").c_str(), 1, ALPM_SIG_PACKAGE, &package), 0);
  EXPECT_EQ(package, nullptr);
  alpm_release(handle);
}

}  // namespace
}  // namespace holonight_transaction_testing
