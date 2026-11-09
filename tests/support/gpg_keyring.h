#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace holonight_transaction_testing {

// A throwaway GPG signing key in a temporary home directory (REQ-C-011). Nothing is committed to the repository:
// the key is generated when the test runs and removed with its home directory. Needs the `gpg` binary.
class GpgKeyring {
 public:
  enum class Trust : std::uint8_t { Ultimate, None };

  // Generates an ed25519 key in `home` (created, mode 0700). Aborts the test process if gpg fails.
  explicit GpgKeyring(std::filesystem::path home,
                      const std::string& userId = "Holonight Fixture <fixture@example.invalid>");
  ~GpgKeyring();
  GpgKeyring(const GpgKeyring&) = delete;
  GpgKeyring& operator=(const GpgKeyring&) = delete;
  GpgKeyring(GpgKeyring&&) = delete;
  GpgKeyring& operator=(GpgKeyring&&) = delete;

  [[nodiscard]] const std::string& fingerprint() const { return fingerprint_; }
  [[nodiscard]] const std::filesystem::path& home() const { return home_; }

  // Writes the detached signature `<file>.sig`.
  void signFile(const std::filesystem::path& file) const;
  // A Signer for RepoBuilder.
  [[nodiscard]] std::function<void(const std::filesystem::path&)> signer() const;

  // Imports the public key into the libalpm keyring directory `gpgDir`; Trust::Ultimate also sets ultimate owner
  // trust, which libalpm needs for "TrustedOnly".
  void trustIn(const std::filesystem::path& gpgDir, Trust trust = Trust::Ultimate) const;

 private:
  void gpg(const std::string& arguments) const;
  void stopAgent() const;

  std::filesystem::path home_;
  std::string fingerprint_;
};

}  // namespace holonight_transaction_testing
