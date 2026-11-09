#pragma once

#include "fixture_root.h"
#include "package_spec.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace holonight_transaction_testing {

// Builds one file:// repository in a FixtureRoot: package files, the <name>.db that a server would serve, and the same
// database copied into the world's sync directory (the helper never refreshes databases, REQ-F-027).
class RepoBuilder {
 public:
  using Signer = std::function<void(const std::filesystem::path& packageFile)>;

  RepoBuilder(const FixtureRoot& world, std::string name);

  // Called with every package file right after it is written; the callback writes `<file>.sig`.
  void setSigner(Signer signer) { signer_ = std::move(signer); }

  // Adds (or replaces, by name) a package and writes its file. Call publish() afterwards.
  RepoBuilder& add(const PackageSpec& spec);
  // Writes <name>.db into the repository directory and into the world's sync directory.
  void publish() const;

  // Mutations that model a broken mirror. They change the file only, not the database entry.
  void corruptPackage(const std::string& name) const;     // flips one byte; size and name stay
  void deletePackageFile(const std::string& name) const;  // the entry stays in the database
  void makeUnreadable(const std::string& name) const;     // mode 000
  void deleteSignature(const std::string& name) const;    // removes <file>.sig

  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] std::filesystem::path dir() const { return world_->repoDir(name_); }
  [[nodiscard]] std::filesystem::path packagePath(const std::string& packageName) const;
  [[nodiscard]] std::string serverUrl() const { return world_->serverUrl(name_); }

 private:
  struct Entry {
    PackageSpec spec;
    BuiltPackage built;
  };

  const FixtureRoot* world_;
  std::string name_;
  std::map<std::string, Entry> entries_;
  Signer signer_;
};

}  // namespace holonight_transaction_testing
