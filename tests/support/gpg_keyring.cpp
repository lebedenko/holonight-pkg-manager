#include "gpg_keyring.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>

namespace holonight_transaction_testing {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "GpgKeyring: " << message << '\n';
  std::abort();
}

std::string quote(const fs::path& path) { return "'" + path.string() + "'"; }

}  // namespace

GpgKeyring::GpgKeyring(fs::path home, const std::string& userId) : home_(std::move(home)) {
  fs::create_directories(home_);
  ::chmod(home_.c_str(), 0700);
  gpg("--pinentry-mode loopback --passphrase '' --quick-generate-key '" + userId + "' ed25519 sign never");
  const fs::path listing = home_ / "fingerprint.txt";
  gpg("--with-colons --fingerprint > " + quote(listing));
  std::ifstream input(listing);
  for (std::string line; std::getline(input, line);) {
    if (line.starts_with("fpr:")) {
      std::istringstream fields(line);
      std::string field;
      for (int i = 0; i < 10 && std::getline(fields, field, ':'); ++i) {
      }
      fingerprint_ = field;
      break;
    }
  }
  if (fingerprint_.empty()) {
    fail("no fingerprint found for the generated key");
  }
}

GpgKeyring::~GpgKeyring() { stopAgent(); }

void GpgKeyring::gpg(const std::string& arguments) const {
  const std::string command = "gpg --homedir " + quote(home_) + " --batch --yes --quiet --no-tty " + arguments + " 2>" +
                              quote(home_ / "gpg.log");
  const int status = std::system(command.c_str());
  if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::ifstream log(home_ / "gpg.log");
    std::ostringstream text;
    text << log.rdbuf();
    fail("gpg failed: " + command + "\n" + text.str());
  }
}

void GpgKeyring::stopAgent() const {
  const std::string command = "gpgconf --homedir " + quote(home_) + " --kill all >/dev/null 2>&1";
  [[maybe_unused]] const int ignored = std::system(command.c_str());
}

void GpgKeyring::signFile(const fs::path& file) const {
  std::error_code error;
  fs::remove(file.string() + ".sig", error);
  gpg("--pinentry-mode loopback --passphrase '' --detach-sign --output " + quote(file.string() + ".sig") + " " +
      quote(file));
}

std::function<void(const fs::path&)> GpgKeyring::signer() const {
  return [this](const fs::path& file) { signFile(file); };
}

void GpgKeyring::trustIn(const fs::path& gpgDir, Trust trust) const {
  fs::create_directories(gpgDir);
  ::chmod(gpgDir.c_str(), 0700);
  const fs::path publicKey = home_ / "public.key";
  gpg("--export --output " + quote(publicKey));
  const std::string base = "gpg --homedir " + quote(gpgDir) + " --batch --yes --quiet --no-tty ";
  const auto run = [&](const std::string& command) {
    if (std::system((command + " >/dev/null 2>&1").c_str()) != 0) {
      fail("gpg failed: " + command);
    }
  };
  run(base + "--import " + quote(publicKey));
  if (trust == Trust::Ultimate) {
    run("printf '%s:6:\\n' " + fingerprint_ + " | " + base + "--import-ownertrust");
  }
  [[maybe_unused]] const int ignored =
      std::system(("gpgconf --homedir " + quote(gpgDir) + " --kill all >/dev/null 2>&1").c_str());
}

}  // namespace holonight_transaction_testing
