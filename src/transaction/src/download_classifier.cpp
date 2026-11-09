#include "download_classifier.h"

#include <algorithm>
#include <cerrno>
#include <optional>
#include <sys/stat.h>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::TransactionErrorCode;

constexpr std::string_view kRetrieving = "failed retrieving file '";
constexpr std::string_view kCouldNotOpen = "Could not open file ";
constexpr std::string_view kCouldntOpen = "Couldn't open file ";

struct FailedFile {
  std::string name;
  std::string detail;  // everything after " : "
};

// `failed retrieving file '<name>' from <where> : <detail>`
std::optional<FailedFile> parseRetrieveFailure(const std::string& line) {
  const auto start = line.find(kRetrieving);
  if (start == std::string::npos) {
    return std::nullopt;
  }
  const auto nameBegin = start + kRetrieving.size();
  const auto nameEnd = line.find('\'', nameBegin);
  if (nameEnd == std::string::npos) {
    return std::nullopt;
  }
  FailedFile failed{.name = line.substr(nameBegin, nameEnd - nameBegin), .detail = {}};
  if (const auto separator = line.find(" : ", nameEnd); separator != std::string::npos) {
    failed.detail = line.substr(separator + 3);
  }
  return failed;
}

bool endsWith(const std::string& text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.ends_with(suffix);
}

}  // namespace

int systemStatError(const std::filesystem::path& path) {
  struct stat info{};
  return ::stat(path.c_str(), &info) == 0 ? 0 : errno;
}

DownloadClassification classifyDownloadFailure(const std::vector<std::string>& logLines, const StatError& statError) {
  for (const std::string& line : logLines) {
    const auto failed = parseRetrieveFailure(line);
    if (!failed) {
      continue;
    }
    DownloadClassification result{.code = TransactionErrorCode::DownloadFailed, .file_name = failed->name};
    if (endsWith(failed->name, ".sig")) {
      result.code = TransactionErrorCode::SignatureInvalid;
    } else if (failed->detail.contains("returned error: 404") || failed->detail.contains("404 Not Found")) {
      result.code = TransactionErrorCode::SyncDatabaseStale;
    } else if (const auto position = std::min(failed->detail.find(kCouldNotOpen), failed->detail.find(kCouldntOpen));
               position != std::string::npos) {
      const std::size_t prefix = failed->detail.compare(position, kCouldNotOpen.size(), kCouldNotOpen) == 0
                                     ? kCouldNotOpen.size()
                                     : kCouldntOpen.size();
      const std::filesystem::path path = failed->detail.substr(position + prefix);
      const int error = statError(path);
      if (error == ENOENT || error == ENOTDIR) {
        result.code = TransactionErrorCode::SyncDatabaseStale;
      }
    }
    return result;
  }
  return DownloadClassification{.code = TransactionErrorCode::DownloadFailed, .file_name = {}};
}

}  // namespace holonight_packages_transaction
