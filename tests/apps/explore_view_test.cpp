#include "ExploreModel.h"
#include "fake_explore_source.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTest>
#include <QtQml/qqml.h>

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using holonight_packages_domain::ExploreSnapshot;
using holonight_packages_domain::ExploreSourceError;
using holonight_packages_domain::ExploreSourceErrorCode;
using holonight_packages_domain::SyncPackage;
using holonight_packages_testing::FakeExploreSource;
using std::chrono::days;
using std::chrono::hours;
using TimePoint = std::chrono::system_clock::time_point;

const TimePoint kNow = TimePoint{} + days(20000);

void rejectQmlWarnings(QQmlEngine& engine) {
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& errors) {
    for (const auto& error : errors) {
      ADD_FAILURE() << qPrintable(error.toString());
    }
  });
}

QUrl qmlSource(const QString& path) { return QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + path); }

SyncPackage makePackage(std::string name, std::string description, std::string repository, std::string version,
                        std::optional<std::string> installed = std::nullopt) {
  return SyncPackage{.name = std::move(name),
                     .version = std::move(version),
                     .repository = std::move(repository),
                     .description = std::move(description),
                     .url = "vim.example.org",
                     .licenses = {"GPL", "MIT"},
                     .dependencies = {"glibc>=2.38"},
                     .optionalDependencies = {"python: Python support"},
                     .downloadSizeBytes = 2048,
                     .installedSizeBytes = 4096,
                     .installedVersion = std::move(installed)};
}

ExploreSnapshot vimSnapshot(TimePoint data_as_of = kNow - hours(2)) {
  return ExploreSnapshot{.packages = {makePackage("vim", "Vi Improved", "core", "9.1-1", "9.0-1"),
                                      makePackage("vim-runtime", "Runtime files", "core", "9.1-1", "9.1-1"),
                                      makePackage("vimb", "Vim-like browser", "extra", "3.7-1"),
                                      makePackage("nano", "Small editor", "core", "8.0-1")},
                         .databasesFound = true,
                         .dataAsOf = data_as_of};
}

class ExploreViewTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    qmlRegisterUncreatableType<ExploreModel>("HolonightPackages", 1, 0, "ExploreModel",
                                             QStringLiteral("Provided by the test"));
  }

  void SetUp() override { rejectQmlWarnings(engine_); }

  void TearDown() override { view_.reset(); }

  ExploreModel& startModel(bool wait = true) {
    model_ = std::make_unique<ExploreModel>(source_, nullptr, [] { return kNow; }, std::chrono::milliseconds{25});
    if (wait) {
      EXPECT_TRUE(QTest::qWaitFor([this] { return !model_->loading(); }, 2000));
    }
    return *model_;
  }

  QQuickItem& createView() {
    QQmlComponent component(&engine_, qmlSource(QStringLiteral("/explore/ExploreView.qml")));
    EXPECT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
    view_.reset(qobject_cast<QQuickItem*>(
        component.createWithInitialProperties({{QStringLiteral("exploreModel"), QVariant::fromValue(model_.get())}})));
    EXPECT_NE(view_, nullptr);
    // Layouts and ListView delegates are only polished inside a window.
    view_->setParentItem(window_.contentItem());
    window_.resize(1300, 800);
    view_->setSize(QSizeF(1300, 800));
    window_.show();
    EXPECT_TRUE(QTest::qWaitForWindowExposed(&window_));
    QCoreApplication::processEvents();
    return *view_;
  }

  void destroyView() { view_.reset(); }

  [[nodiscard]] QObject* child(const char* name) const {
    auto* object = view_->findChild<QObject*>(QString::fromLatin1(name));
    EXPECT_NE(object, nullptr) << name;
    return object;
  }

  // Loader content is parented visually, so search the visual tree as well as the QObject tree.
  [[nodiscard]] bool exists(const char* name) const {
    const QString wanted = QString::fromLatin1(name);
    if (view_->findChild<QObject*>(wanted) != nullptr) {
      return true;
    }
    std::vector<QQuickItem*> pending{view_.get()};
    while (!pending.empty()) {
      QQuickItem* item = pending.back();
      pending.pop_back();
      if (item->objectName() == wanted) {
        return true;
      }
      for (QQuickItem* child : item->childItems()) {
        pending.push_back(child);
      }
    }
    return false;
  }

  [[nodiscard]] bool isVisible(const char* name) const { return child(name)->property("visible").toBool(); }

  // Types into the real search field, then waits for the debounced search.
  void typeQuery(const QString& text) {
    auto* field = qobject_cast<QQuickItem*>(child("exploreSearchField"));
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    const int before = model_->searchCount();
    for (const QChar character : text) {
      QTest::keyClick(&window_, character.toLatin1());
    }
    ASSERT_TRUE(QTest::qWaitFor([&] { return model_->searchCount() > before; }, 2000));
    QCoreApplication::processEvents();
  }

  [[nodiscard]] std::vector<QQuickItem*> rows() const {
    auto* list = child("exploreList");
    std::vector<QQuickItem*> result;
    const int count = list->property("count").toInt();
    for (int index = 0; index < count; ++index) {
      QQuickItem* row = nullptr;
      EXPECT_TRUE(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, index)));
      result.push_back(row);
    }
    return result;
  }

  [[nodiscard]] bool rowsReady(int expected) const {
    if (!exists("exploreList")) {
      return false;
    }
    const auto current = rows();
    if (std::cmp_not_equal(current.size(), expected)) {
      return false;
    }
    return std::ranges::all_of(current, [](const QQuickItem* row) { return row != nullptr; });
  }

  // Every rawText shown by labels under `root`.
  static QStringList shownTexts(QObject* root) {
    // Walks the visual tree: Loader and Repeater content is parented visually, not through QObject children.
    QStringList texts;
    std::vector<QQuickItem*> pending{qobject_cast<QQuickItem*>(root)};
    while (!pending.empty()) {
      QQuickItem* item = pending.back();
      pending.pop_back();
      const QVariant text = item->property("rawText");
      if (text.isValid() && item->isVisible()) {
        texts << text.toString();
      }
      for (QQuickItem* child : item->childItems()) {
        pending.push_back(child);
      }
    }
    return texts;
  }

  [[nodiscard]] FakeExploreSource& source() const { return *source_; }
  [[nodiscard]] ExploreModel& model() const { return *model_; }
  [[nodiscard]] QQuickWindow& window() { return window_; }

 private:
  std::shared_ptr<FakeExploreSource> source_ = std::make_shared<FakeExploreSource>();
  std::unique_ptr<ExploreModel> model_;
  QQmlEngine engine_;
  QQuickWindow window_;
  // Declared last so the view is destroyed before the window, engine and model it uses.
  std::unique_ptr<QQuickItem> view_;
};

TEST_F(ExploreViewTest, FreshViewShowsHintAndEnabledSearchField) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();

  EXPECT_TRUE(isVisible("hintState"));
  EXPECT_FALSE(isVisible("noMatchesState"));
  EXPECT_TRUE(child("exploreSearchField")->property("enabled").toBool());
  EXPECT_TRUE(child("exploreReloadButton")->property("enabled").toBool());
}

TEST_F(ExploreViewTest, LoadingStateDisablesSearchAndReload) {
  source().setBlocking(true);
  startModel(false);
  createView();

  EXPECT_TRUE(isVisible("loadingState"));
  EXPECT_FALSE(child("exploreSearchField")->property("enabled").toBool());
  EXPECT_FALSE(child("exploreReloadButton")->property("enabled").toBool());
  EXPECT_TRUE(isVisible("exploreProgressBar"));

  source().release();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !model().loading(); }, 2000));
  EXPECT_TRUE(child("exploreSearchField")->property("enabled").toBool());
  EXPECT_TRUE(child("exploreReloadButton")->property("enabled").toBool());
}

TEST_F(ExploreViewTest, TypingShowsRankedRowsExposingAllFields) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();

  typeQuery(QStringLiteral("vim"));

  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }));
  const auto current = rows();
  EXPECT_EQ(current[0]->property("name").toString(), "vim");
  EXPECT_EQ(current[0]->property("availableVersion").toString(), "9.1-1");
  EXPECT_EQ(current[0]->property("repository").toString(), "core");
  EXPECT_EQ(current[0]->property("downloadSizeLabel").toString(), "2 KiB");
  EXPECT_EQ(current[0]->property("installedBadgeText").toString(), "Installed 9.0-1");
  EXPECT_TRUE(current[0]->property("installedVersionDiffers").toBool());
  EXPECT_EQ(current[1]->property("name").toString(), "vim-runtime");
  EXPECT_EQ(current[1]->property("installedBadgeText").toString(), "Installed");
  EXPECT_EQ(current[2]->property("name").toString(), "vimb");
  EXPECT_FALSE(current[2]->property("isInstalled").toBool());
  EXPECT_TRUE(isVisible("exploreTable"));
  EXPECT_FALSE(isVisible("hintState"));
}

TEST_F(ExploreViewTest, NoMatchesShowsMessageWithQuery) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();

  typeQuery(QStringLiteral("zzzz"));

  EXPECT_TRUE(isVisible("noMatchesState"));
  EXPECT_TRUE(child("noMatchesState")->property("titleText").toString().contains("zzzz"));
  EXPECT_FALSE(isVisible("exploreTable"));
}

TEST_F(ExploreViewTest, ClickingARowUpdatesTheReadOnlyDetailsPanel) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();
  typeQuery(QStringLiteral("vim"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }));

  EXPECT_TRUE(isVisible("packageDetailEmptyState"));

  auto* second = rows()[1];
  QTest::mouseClick(&window(), Qt::LeftButton, {}, second->mapToScene(QPointF(second->width() / 2, 10)).toPoint());
  ASSERT_TRUE(QTest::qWaitFor([this] { return model().currentRow() == 1; }));
  QCoreApplication::processEvents();

  const auto texts = shownTexts(child("exploreDetailPanel"));
  EXPECT_TRUE(texts.contains("vim-runtime"));
  EXPECT_TRUE(texts.contains("Runtime files"));
  EXPECT_TRUE(texts.contains("GPL, MIT"));
  EXPECT_TRUE(texts.contains("vim.example.org"));
  EXPECT_TRUE(texts.contains("glibc>=2.38"));
  EXPECT_TRUE(texts.contains("python: Python support"));

  auto* first = rows()[0];
  QTest::mouseClick(&window(), Qt::LeftButton, {}, first->mapToScene(QPointF(first->width() / 2, 10)).toPoint());
  ASSERT_TRUE(QTest::qWaitFor([this] { return model().currentRow() == 0; }));
  QCoreApplication::processEvents();
  EXPECT_TRUE(shownTexts(child("exploreDetailPanel")).contains("Vi Improved"));
}

TEST_F(ExploreViewTest, DetailsPanelHasNoActionsOrInstalledOnlyContent) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();
  typeQuery(QStringLiteral("vim"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }));
  model().setCurrentRow(0);
  QCoreApplication::processEvents();

  EXPECT_FALSE(exists("packageDetailRemoveButton"));
  EXPECT_FALSE(exists("packageDetailMoreOptionsButton"));
  EXPECT_FALSE(exists("packageDetailFilesLink"));
  EXPECT_FALSE(exists("packageDetailWebsiteLink"));
  // The panel shares the Installed panel's components.
  EXPECT_TRUE(exists("packageDetailScrollView"));
  EXPECT_TRUE(exists("packageDetailOriginBadge"));

  const auto texts = shownTexts(child("exploreDetailPanel"));
  for (const QString& forbidden : {"Remove", "Installed", "Reason", "Required by", "Local state"}) {
    EXPECT_FALSE(texts.contains(forbidden)) << qPrintable(forbidden);
  }
  EXPECT_FALSE(exists("packageRowCheckBox"));
}

TEST_F(ExploreViewTest, ThirdPartySyncRepositoryUsesItsOwnBadgeAndScopeNote) {
  source().enqueue(ExploreSnapshot{.packages = {makePackage("helper", "Utility", "community-custom", "1-1")},
                                   .databasesFound = true,
                                   .dataAsOf = kNow - hours(2)});
  startModel();
  createView();
  typeQuery(QStringLiteral("helper"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(1); }));
  model().setCurrentRow(0);
  QCoreApplication::processEvents();

  EXPECT_EQ(child("packageDetailOriginBadge")->property("text").toString(), QStringLiteral("community-custom"));
  EXPECT_TRUE(child("exploreHeadlineRepositoryScope")
                  ->property("rawText")
                  .toString()
                  .contains(QStringLiteral("Configured sync repositories")));
}

TEST_F(ExploreViewTest, KeyboardDownSelectsNextRow) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();
  typeQuery(QStringLiteral("vim"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }));
  auto* list = qobject_cast<QQuickItem*>(child("exploreList"));
  list->forceActiveFocus();

  QTest::keyClick(&window(), Qt::Key_Down);
  QTest::keyClick(&window(), Qt::Key_Down);

  EXPECT_EQ(model().currentRow(), 1);
}

TEST_F(ExploreViewTest, StaleHintAppearsOnlyForOldData) {
  source().enqueue(vimSnapshot(kNow - hours(24 * 8)));
  startModel();
  createView();
  EXPECT_TRUE(isVisible("exploreStaleHint"));
  EXPECT_EQ(child("exploreStaleHint")->property("text").toString(), ExploreModel::staleHintText());
  destroyView();

  source().enqueue(vimSnapshot(kNow - hours(24 * 7)));
  model().reload();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !model().loading(); }, 2000));
  createView();
  EXPECT_FALSE(isVisible("exploreStaleHint"));
}

TEST_F(ExploreViewTest, FailedReloadShowsBannerAndKeepsResults) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();
  typeQuery(QStringLiteral("vim"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }));
  EXPECT_FALSE(isVisible("exploreReloadBanner"));

  source().enqueue(std::unexpected(
      ExploreSourceError{.code = ExploreSourceErrorCode::DatabaseOpenFailed, .message = "disk on fire"}));
  ASSERT_TRUE(QMetaObject::invokeMethod(child("exploreReloadButton"), "clicked"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return !model().loading(); }, 2000));
  QCoreApplication::processEvents();

  EXPECT_TRUE(isVisible("exploreReloadBanner"));
  EXPECT_TRUE(child("exploreReloadBanner")->property("text").toString().contains("disk on fire"));
  EXPECT_TRUE(rowsReady(3));
}

TEST_F(ExploreViewTest, InitialFailureShowsErrorState) {
  source().enqueue(std::unexpected(
      ExploreSourceError{.code = ExploreSourceErrorCode::DatabaseOpenFailed, .message = "cannot open"}));
  startModel();
  createView();

  EXPECT_TRUE(isVisible("errorState"));
  EXPECT_EQ(child("errorState")->property("descriptionText").toString(), "cannot open");
  EXPECT_FALSE(child("exploreSearchField")->property("enabled").toBool());
  EXPECT_TRUE(child("exploreReloadButton")->property("enabled").toBool());
}

TEST_F(ExploreViewTest, NoDatabasesShowsItsState) {
  source().enqueue(ExploreSnapshot{.packages = {}, .databasesFound = false, .dataAsOf = {}});
  startModel();
  createView();

  EXPECT_TRUE(isVisible("noDatabasesState"));
  EXPECT_FALSE(child("exploreSearchField")->property("enabled").toBool());
}

TEST_F(ExploreViewTest, CapFooterShowsWhenMatchesExceedTheCap) {
  ExploreSnapshot snapshot{.packages = {}, .databasesFound = true, .dataAsOf = kNow - hours(1)};
  for (int i = 0; i < 520; ++i) {
    snapshot.packages.push_back(makePackage("pkg" + std::to_string(1000 + i), "", "extra", "1-1"));
  }
  source().enqueue(std::move(snapshot));
  startModel();
  createView();

  typeQuery(QStringLiteral("pkg"));

  EXPECT_TRUE(isVisible("exploreCapFooter"));
  EXPECT_TRUE(child("exploreCapFooter")->property("rawText").toString().contains("520"));
}

TEST_F(ExploreViewTest, QueryAndSelectionSurviveViewRecreation) {
  source().enqueue(vimSnapshot());
  startModel();
  createView();
  typeQuery(QStringLiteral("vim"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }));
  model().setCurrentRow(2);

  destroyView();
  createView();

  EXPECT_EQ(child("exploreSearchField")->property("text").toString(), "vim");
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }));
  EXPECT_EQ(model().currentRow(), 2);
  EXPECT_TRUE(shownTexts(child("exploreDetailPanel")).contains("vimb"));
}

}  // namespace
