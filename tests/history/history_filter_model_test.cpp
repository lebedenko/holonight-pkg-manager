#include "HistoryFilterModel.h"
#include "HistoryModel.h"
#include "counting_history_source.h"
#include "history_test_entries.h"

#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <gtest/gtest.h>
#include <memory>

namespace {

using holonight_packages_domain::HistoryReadResult;
using holonight_test::CountingHistorySource;
using holonight_test::historyEntry;
using holonight_test::resetWith;
using std::chrono::sys_days;
using namespace std::chrono_literals;

constexpr int kWaitMs = 5000;

class HistoryFilterModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // T1 (older) touches linux and linux-headers; T2 (newer) touches git.
    source_->enqueue(resetWith({
        historyEntry(sys_days{2026y / 11 / 22}, 1h, {"linux", "linux-headers"}),
        historyEntry(sys_days{2026y / 11 / 23}, 1h, {"git"}),
    }));
    filter_.setSourceModel(model_.get());
    model_->ensureLoaded();
    ASSERT_TRUE(QTest::qWaitFor([this] { return model_->loaded() && !model_->loading(); }, kWaitMs));
  }

  [[nodiscard]] HistoryFilterModel& filter() { return filter_; }
  [[nodiscard]] HistoryModel& model() { return *model_; }
  [[nodiscard]] CountingHistorySource& source() { return *source_; }

  [[nodiscard]] QStringList visibleNames() const {
    QStringList names;
    for (int row = 0; row < filter_.rowCount(); ++row) {
      names.append(filter_.get(row).value("packageNames").toStringList().join(','));
    }
    return names;
  }

 private:
  std::shared_ptr<CountingHistorySource> source_ = std::make_shared<CountingHistorySource>();
  std::unique_ptr<HistoryModel> model_ =
      std::make_unique<HistoryModel>(source_, HistoryModelOptions{.current_uid = 1000, .resolve_user = {}});
  HistoryFilterModel filter_;
};

TEST_F(HistoryFilterModelTest, FiltersByPackageName) {
  EXPECT_EQ(visibleNames(), (QStringList{"git", "linux,linux-headers"}));
  filter().setSearchText(QStringLiteral("linux"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return visibleNames() == QStringList{"linux,linux-headers"}; }, kWaitMs));
  filter().setSearchText(QStringLiteral("git"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return visibleNames() == QStringList{"git"}; }, kWaitMs));
  filter().setSearchText(QString());
  ASSERT_TRUE(QTest::qWaitFor([this] { return filter().rowCount() == 2; }, kWaitMs));
  EXPECT_EQ(visibleNames(), (QStringList{"git", "linux,linux-headers"}));
}

TEST_F(HistoryFilterModelTest, FilterIsACaseInsensitiveSubstring) {
  filter().setSearchText(QStringLiteral("LINUX"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return visibleNames() == QStringList{"linux,linux-headers"}; }, kWaitMs));
  filter().setSearchText(QStringLiteral("headers"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return visibleNames() == QStringList{"linux,linux-headers"}; }, kWaitMs));
  filter().setSearchText(QStringLiteral("nothing"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return filter().rowCount() == 0; }, kWaitMs));
  EXPECT_EQ(filter().currentRow(), -1);
  EXPECT_TRUE(filter().currentEntry().isEmpty());
}

TEST_F(HistoryFilterModelTest, SelectsTheNewestRowByDefault) {
  EXPECT_EQ(filter().currentRow(), 0);
  EXPECT_EQ(filter().currentEntry().value("packageNames").toStringList(), QStringList{"git"});
}

TEST_F(HistoryFilterModelTest, SelectionSurvivesAnInsertAtRowZero) {
  filter().setCurrentRow(1);
  ASSERT_EQ(filter().currentEntry().value("packageNames").toStringList(), (QStringList{"linux", "linux-headers"}));
  const auto sequence = filter().currentEntry().value("sequence").value<quint32>();

  HistoryReadResult delta;
  delta.append = {historyEntry(sys_days{2026y / 11 / 24}, 1h, {"vim"})};
  source().enqueue(delta);
  QSignalSpy entry_changed(&filter(), &HistoryFilterModel::currentEntryChanged);
  model().refresh();
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 2 && !model().loading(); }, kWaitMs));

  EXPECT_EQ(filter().rowCount(), 3);
  EXPECT_EQ(filter().currentRow(), 2);
  EXPECT_EQ(filter().currentEntry().value("sequence").value<quint32>(), sequence);
  EXPECT_EQ(entry_changed.count(), 0);
}

TEST_F(HistoryFilterModelTest, SelectionSurvivesAFilterThatKeepsIt) {
  filter().setCurrentRow(1);
  filter().setSearchText(QStringLiteral("linux"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return filter().currentRow() == 0; }, kWaitMs));
  EXPECT_EQ(filter().currentEntry().value("packageNames").toStringList(), (QStringList{"linux", "linux-headers"}));
  filter().setSearchText(QString());
  ASSERT_TRUE(QTest::qWaitFor([this] { return filter().currentRow() == 1; }, kWaitMs));
}

TEST_F(HistoryFilterModelTest, GetOutsideTheRowsIsEmpty) {
  EXPECT_TRUE(filter().get(-1).isEmpty());
  EXPECT_TRUE(filter().get(5).isEmpty());
  EXPECT_TRUE(filter().get(0).contains("summary"));
}

}  // namespace
