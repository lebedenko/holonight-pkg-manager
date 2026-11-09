#include "repo_builder.h"

#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace holonight_transaction_testing {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "RepoBuilder: " << message << '\n';
  std::abort();
}

void writeDirectoryEntry(archive* writer, const std::string& path) {
  archive_entry* entry = archive_entry_new();
  archive_entry_set_pathname(entry, path.c_str());
  archive_entry_set_filetype(entry, AE_IFDIR);
  archive_entry_set_perm(entry, 0755);
  archive_entry_set_mtime(entry, 1700000000, 0);
  archive_write_header(writer, entry);
  archive_entry_free(entry);
}

void writeFileEntry(archive* writer, const std::string& path, const std::string& content) {
  archive_entry* entry = archive_entry_new();
  archive_entry_set_pathname(entry, path.c_str());
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_perm(entry, 0644);
  archive_entry_set_mtime(entry, 1700000000, 0);
  archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));
  archive_write_header(writer, entry);
  archive_write_data(writer, content.data(), content.size());
  archive_entry_free(entry);
}

}  // namespace

RepoBuilder::RepoBuilder(const FixtureRoot& world, std::string name) : world_(&world), name_(std::move(name)) {
  fs::create_directories(dir());
}

RepoBuilder& RepoBuilder::add(const PackageSpec& spec) {
  if (const auto existing = entries_.find(spec.name); existing != entries_.end()) {
    std::error_code error;
    fs::remove(existing->second.built.path, error);
    fs::remove(existing->second.built.path.string() + ".sig", error);
  }
  BuiltPackage built = writePackageFile(spec, dir());
  if (signer_) {
    signer_(built.path);
  }
  entries_[spec.name] = Entry{.spec = spec, .built = std::move(built)};
  return *this;
}

void RepoBuilder::publish() const {
  const fs::path database = dir() / (name_ + ".db");
  archive* writer = archive_write_new();
  if (archive_write_add_filter_gzip(writer) != ARCHIVE_OK ||
      archive_write_set_format_pax_restricted(writer) != ARCHIVE_OK ||
      archive_write_open_filename(writer, database.c_str()) != ARCHIVE_OK) {
    fail(std::string("open database: ") + archive_error_string(writer));
  }
  for (const auto& [name, entry] : entries_) {
    const std::string directory = entry.spec.name + "-" + entry.spec.version;
    writeDirectoryEntry(writer, directory + "/");
    writeFileEntry(writer, directory + "/desc", syncDescEntry(entry.spec, entry.built));
  }
  if (archive_write_close(writer) != ARCHIVE_OK) {
    fail(std::string("close database: ") + archive_error_string(writer));
  }
  archive_write_free(writer);
  std::error_code error;
  fs::create_directories(world_->syncDbPath(), error);
  fs::copy_file(database, world_->syncDbPath() / (name_ + ".db"), fs::copy_options::overwrite_existing, error);
  if (error) {
    fail("cannot copy the database into the sync directory: " + error.message());
  }
}

fs::path RepoBuilder::packagePath(const std::string& packageName) const {
  const auto entry = entries_.find(packageName);
  if (entry == entries_.end()) {
    fail("unknown package " + packageName);
  }
  return entry->second.built.path;
}

void RepoBuilder::corruptPackage(const std::string& name) const {
  const fs::path path = packagePath(name);
  std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
  file.seekg(0, std::ios::end);
  const std::streamoff middle = static_cast<std::streamoff>(file.tellg()) / 2;
  file.seekg(middle);
  char byte = 0;
  file.get(byte);
  byte = static_cast<char>(byte ^ 0xFF);
  file.seekp(middle);
  file.put(byte);
}

void RepoBuilder::deletePackageFile(const std::string& name) const {
  std::error_code error;
  fs::remove(packagePath(name), error);
}

void RepoBuilder::makeUnreadable(const std::string& name) const {
  fs::permissions(packagePath(name), fs::perms::none, fs::perm_options::replace);
}

void RepoBuilder::deleteSignature(const std::string& name) const {
  std::error_code error;
  fs::remove(packagePath(name).string() + ".sig", error);
}

}  // namespace holonight_transaction_testing
