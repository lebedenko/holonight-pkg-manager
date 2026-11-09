#include "download_classifier.h"

#include <cerrno>
#include <gtest/gtest.h>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::TransactionErrorCode;

DownloadClassification classify(const std::vector<std::string>& lines, int statResult = 0) {
  return classifyDownloadFailure(lines, [statResult](const std::filesystem::path&) { return statResult; });
}

// The lines below are the exact texts recorded by spike S-4 for pacman 7.1 (see SPIKES.md).
TEST(DownloadClassifierTest, Http404IsStale) {
  const auto result = classify({
      "failed retrieving file 'app-1.0-1-any.pkg.tar.zst' from 127.0.0.1:18766 : The requested URL returned "
      "error: 404",
      "failed to retrieve some files",
  });
  EXPECT_EQ(result.code, TransactionErrorCode::SyncDatabaseStale);
  EXPECT_EQ(result.file_name, "app-1.0-1-any.pkg.tar.zst");
  EXPECT_EQ(classify({"failed retrieving file 'x.pkg.tar.zst' from h : 404 Not Found"}).code,
            TransactionErrorCode::SyncDatabaseStale);
}

TEST(DownloadClassifierTest, OtherHttpErrorsAndTimeoutsAreFailedNotStale) {
  EXPECT_EQ(classify({"failed retrieving file 'a.pkg.tar.zst' from h : The requested URL returned error: 500"}).code,
            TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(classify({"failed retrieving file 'a.pkg.tar.zst' from h : The requested URL returned error: 403"}).code,
            TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(classify({"failed retrieving file 'a.pkg.tar.zst' from h : Operation too slow. Less than 1 bytes/sec "
                      "transferred the last 10 seconds"})
                .code,
            TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(classify({"failed retrieving file 'a.pkg.tar.zst' from h : Could not resolve host: h"}).code,
            TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(classify({"failed retrieving file 'a.pkg.tar.zst' from h : SSL certificate problem"}).code,
            TransactionErrorCode::DownloadFailed);
}

TEST(DownloadClassifierTest, ALocalFileThatIsAbsentIsStaleAndOneThatIsUnreadableIsNot) {
  const std::string line =
      "failed retrieving file 'app-1.0-1-any.pkg.tar.zst' from disk : Could not open file "
      "/srv/repo/app-1.0-1-any.pkg.tar.zst";
  EXPECT_EQ(classify({line}, ENOENT).code, TransactionErrorCode::SyncDatabaseStale);
  EXPECT_EQ(classify({line}, ENOTDIR).code, TransactionErrorCode::SyncDatabaseStale);
  EXPECT_EQ(classify({line}, EACCES).code, TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(classify({line}, 0).code, TransactionErrorCode::DownloadFailed);  // it exists now: not stale
}

TEST(DownloadClassifierTest, TheStatTargetIsThePathFromTheLogLine) {
  std::filesystem::path seen;
  const auto result = classifyDownloadFailure(
      {"failed retrieving file 'a.pkg.tar.zst' from disk : Could not open file /srv/repo/a.pkg.tar.zst"},
      [&seen](const std::filesystem::path& path) {
        seen = path;
        return ENOENT;
      });
  EXPECT_EQ(seen, "/srv/repo/a.pkg.tar.zst");
  EXPECT_EQ(result.code, TransactionErrorCode::SyncDatabaseStale);
}

TEST(DownloadClassifierTest, AMissingSignatureFileIsASignatureProblemNeverStale) {
  const auto result = classify(
      {
          "failed retrieving file 'app-1.0-1-any.pkg.tar.zst.sig' from disk : Could not open "
          "file /srv/repo/app-1.0-1-any.pkg.tar.zst.sig",
      },
      ENOENT);
  EXPECT_EQ(result.code, TransactionErrorCode::SignatureInvalid);
  EXPECT_EQ(result.file_name, "app-1.0-1-any.pkg.tar.zst.sig");
  EXPECT_EQ(classify({"failed retrieving file 'a.pkg.tar.zst.sig' from h : returned error: 404"}).code,
            TransactionErrorCode::SignatureInvalid);
}

TEST(DownloadClassifierTest, UnmatchedTextIsNeverStale) {
  EXPECT_EQ(classify({}).code, TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(classify({"something unrelated", "error: 404"}).code, TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(classify({"failed retrieving file 'a.pkg.tar.zst' from h : mysterious"}).code,
            TransactionErrorCode::DownloadFailed);
}

TEST(DownloadClassifierTest, TheFirstFailedFileDecides) {
  const auto result = classify({
      "failed retrieving file 'a.pkg.tar.zst' from h : returned error: 500",
      "failed retrieving file 'b.pkg.tar.zst' from h : returned error: 404",
  });
  EXPECT_EQ(result.code, TransactionErrorCode::DownloadFailed);
  EXPECT_EQ(result.file_name, "a.pkg.tar.zst");
}

TEST(DownloadClassifierTest, TheSystemStatReportsMissingAndPresentPaths) {
  EXPECT_EQ(systemStatError("/definitely/not/here"), ENOENT);
  EXPECT_EQ(systemStatError("/"), 0);
}

}  // namespace
}  // namespace holonight_packages_transaction
