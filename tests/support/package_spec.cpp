#include "package_spec.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>

#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <set>
#include <sstream>

namespace holonight_transaction_testing {
namespace {

constexpr std::time_t kBuildDate = 1700000000;

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "package_spec: " << message << '\n';
  std::abort();
}

void writeEntry(archive* writer, const std::string& path, const std::string& content, mode_t type, int permissions) {
  archive_entry* entry = archive_entry_new();
  archive_entry_set_pathname(entry, path.c_str());
  archive_entry_set_filetype(entry, type);
  archive_entry_set_perm(entry, permissions);
  archive_entry_set_mtime(entry, kBuildDate, 0);
  archive_entry_set_uid(entry, 0);
  archive_entry_set_gid(entry, 0);
  archive_entry_set_size(entry, type == AE_IFREG ? static_cast<la_int64_t>(content.size()) : 0);
  if (archive_write_header(writer, entry) != ARCHIVE_OK) {
    fail(std::string("write header: ") + archive_error_string(writer));
  }
  if (type == AE_IFREG && !content.empty() && archive_write_data(writer, content.data(), content.size()) < 0) {
    fail(std::string("write data: ") + archive_error_string(writer));
  }
  archive_entry_free(entry);
}

std::string pkginfo(const PackageSpec& spec, std::uint64_t installedSize) {
  std::ostringstream out;
  out << "pkgname = " << spec.name << "\npkgbase = " << spec.name << "\npkgver = " << spec.version
      << "\npkgdesc = " << spec.description << "\nurl = https://example.invalid/" << spec.name
      << "\nbuilddate = " << kBuildDate << "\npackager = Fixture <fixture@example.invalid>\nsize = " << installedSize
      << "\narch = " << spec.architecture << "\nlicense = GPL-3.0-or-later\n";
  const auto list = [&out](const char* key, const std::vector<std::string>& values) {
    for (const std::string& value : values) {
      out << key << " = " << value << "\n";
    }
  };
  list("group", spec.groups);
  list("provides", spec.provides);
  list("depend", spec.depends);
  list("conflict", spec.conflicts);
  list("replaces", spec.replaces);
  list("backup", spec.backup);
  return out.str();
}

std::uint64_t installedSizeOf(const PackageSpec& spec) {
  std::uint64_t total = 0;
  for (const auto& [path, content] : spec.files) {
    total += content.size();
  }
  return total;
}

// Every parent directory of the package files, in a stable order, without the root itself.
std::set<std::string> directoriesOf(const PackageSpec& spec) {
  std::set<std::string> directories;
  for (const auto& [path, content] : spec.files) {
    for (std::filesystem::path parent = std::filesystem::path(path).parent_path(); !parent.empty();
         parent = parent.parent_path()) {
      directories.insert(parent.string());
    }
  }
  return directories;
}

void appendList(std::ostringstream& out, const char* key, const std::vector<std::string>& values) {
  if (values.empty()) {
    return;
  }
  out << "%" << key << "%\n";
  for (const std::string& value : values) {
    out << value << "\n";
  }
  out << "\n";
}

}  // namespace

BuiltPackage writePackageFile(const PackageSpec& spec, const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  const std::filesystem::path path = directory / spec.fileName();
  archive* writer = archive_write_new();
  if (archive_write_add_filter_zstd(writer) != ARCHIVE_OK ||
      archive_write_set_format_pax_restricted(writer) != ARCHIVE_OK ||
      archive_write_open_filename(writer, path.c_str()) != ARCHIVE_OK) {
    fail(std::string("open package: ") + archive_error_string(writer));
  }
  const std::uint64_t installedSize = installedSizeOf(spec);
  writeEntry(writer, ".PKGINFO", pkginfo(spec, installedSize), AE_IFREG, 0644);
  for (const std::string& dir : directoriesOf(spec)) {
    writeEntry(writer, dir + "/", "", AE_IFDIR, 0755);
  }
  for (const auto& [file, content] : spec.files) {
    writeEntry(writer, file, content, AE_IFREG, 0644);
  }
  if (archive_write_close(writer) != ARCHIVE_OK) {
    fail(std::string("close package: ") + archive_error_string(writer));
  }
  archive_write_free(writer);

  QFile file(QString::fromStdString(path.string()));
  if (!file.open(QIODevice::ReadOnly)) {
    fail("cannot reread " + path.string());
  }
  const QByteArray bytes = file.readAll();
  return BuiltPackage{
      .path = path,
      .sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString(),
      .compressedSize = static_cast<std::uint64_t>(bytes.size()),
      .installedSize = installedSize,
  };
}

std::string syncDescEntry(const PackageSpec& spec, const BuiltPackage& built) {
  std::ostringstream out;
  out << "%FILENAME%\n"
      << spec.fileName() << "\n\n%NAME%\n"
      << spec.name << "\n\n%BASE%\n"
      << spec.name << "\n\n%VERSION%\n"
      << spec.version << "\n\n%DESC%\n"
      << spec.description << "\n\n%CSIZE%\n"
      << built.compressedSize << "\n\n%ISIZE%\n"
      << built.installedSize << "\n\n%SHA256SUM%\n"
      << built.sha256 << "\n\n%URL%\nhttps://example.invalid/" << spec.name
      << "\n\n%LICENSE%\nGPL-3.0-or-later\n\n%ARCH%\n"
      << spec.architecture << "\n\n%BUILDDATE%\n"
      << kBuildDate << "\n\n%PACKAGER%\nFixture <fixture@example.invalid>\n\n";
  appendList(out, "GROUPS", spec.groups);
  appendList(out, "REPLACES", spec.replaces);
  appendList(out, "DEPENDS", spec.depends);
  appendList(out, "CONFLICTS", spec.conflicts);
  appendList(out, "PROVIDES", spec.provides);
  return out.str();
}

std::string localDescEntry(const PackageSpec& spec, InstallReason reason) {
  std::ostringstream out;
  out << "%NAME%\n"
      << spec.name << "\n\n%VERSION%\n"
      << spec.version << "\n\n%BASE%\n"
      << spec.name << "\n\n%DESC%\n"
      << spec.description << "\n\n%URL%\nhttps://example.invalid/" << spec.name << "\n\n%ARCH%\n"
      << spec.architecture << "\n\n%BUILDDATE%\n"
      << kBuildDate << "\n\n%INSTALLDATE%\n"
      << kBuildDate << "\n\n%PACKAGER%\nFixture <fixture@example.invalid>\n\n%SIZE%\n"
      << installedSizeOf(spec) << "\n\n";
  if (reason == InstallReason::Dependency) {
    out << "%REASON%\n1\n\n";
  }
  out << "%VALIDATION%\nnone\n\n";
  appendList(out, "GROUPS", spec.groups);
  appendList(out, "REPLACES", spec.replaces);
  appendList(out, "DEPENDS", spec.depends);
  appendList(out, "CONFLICTS", spec.conflicts);
  appendList(out, "PROVIDES", spec.provides);
  return out.str();
}

std::string localFilesEntry(const PackageSpec& spec) {
  std::ostringstream out;
  out << "%FILES%\n";
  for (const std::string& dir : directoriesOf(spec)) {
    out << dir << "/\n";
  }
  for (const auto& [file, content] : spec.files) {
    out << file << "\n";
  }
  if (!spec.backup.empty()) {
    out << "\n%BACKUP%\n";
    for (const std::string& file : spec.backup) {
      const auto found = spec.files.find(file);
      const std::string content = found == spec.files.end() ? std::string() : found->second;
      const QByteArray digest =
          QCryptographicHash::hash(QByteArray::fromStdString(content), QCryptographicHash::Md5).toHex();
      out << file << "\t" << digest.toStdString() << "\n";
    }
  }
  return out.str();
}

}  // namespace holonight_transaction_testing
