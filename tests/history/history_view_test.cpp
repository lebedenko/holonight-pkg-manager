#include "HistoryFilterModel.h"
#include "HistoryModel.h"
#include "counting_history_source.h"
#include "history_test_entries.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QTest>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using holonight_packages_domain::HistoryAction;
using holonight_packages_domain::HistoryActionKind;
using holonight_packages_domain::HistoryEntry;
using holonight_packages_domain::HistoryMessage;
using holonight_packages_domain::HistoryOrigin;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistoryReadMode;
using holonight_packages_domain::HistoryReadResult;
using holonight_packages_domain::HistorySeverity;
using holonight_test::CountingHistorySource;
using holonight_test::historyEntry;
using holonight_test::resetWith;
using holonight_test::ScopedTimeZone;
using std::chrono::sys_days;
using namespace std::chrono_literals;

constexpr int kWaitMs = 5000;
constexpr std::uint32_t kCurrentUid = 1000;

QUrl qmlSource(const QString& path) {
#ifdef HOLONIGHT_COMPILED_QML_ACCEPTANCE
  return {QStringLiteral("qrc:/HolonightPackages") + path};
#else
  return QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + path);
#endif
}

HistoryOrigin packagesOrigin() {
  return HistoryOrigin{.kind = HistoryOriginKind::Packages, .uid = kCurrentUid, .command = {}};
}

// Two actions, a hook on each side of the transaction and one warning (REQ-F-030).
HistoryEntry detailedEntry() {
  HistoryEntry entry = historyEntry(sys_days{2026y / 11 / 23}, 8h + 5min, {"foo"});
  entry.origin = packagesOrigin();
  entry.actions.push_back(
      HistoryAction{.kind = HistoryActionKind::Installed, .name = "bar", .old_version = {}, .new_version = "3.0-1"});
  entry.pre_hooks = {"systemd-sysusers.hook"};
  entry.hooks = {"update-desktop-database.hook"};
  entry.messages = {
      HistoryMessage{.severity = HistorySeverity::Warning, .text = "/etc/foo.conf installed as /etc/foo.conf.pacnew"},
  };
  return entry;
}

HistoryEntry pacmanEntry() {
  HistoryEntry entry = historyEntry(sys_days{2026y / 11 / 22}, 21h, {"vim"});
  entry.origin =
      HistoryOrigin{.kind = HistoryOriginKind::Pacman, .uid = std::nullopt, .command = "pacman --sync --needed vim"};
  return entry;
}

// Every item under `root` in the visual tree: Loader and Repeater content is parented visually only.
std::vector<QQuickItem*> visualTree(QQuickItem* root) {
  std::vector<QQuickItem*> items;
  std::vector<QQuickItem*> pending{root};
  while (!pending.empty()) {
    QQuickItem* item = pending.back();
    pending.pop_back();
    items.push_back(item);
    for (QQuickItem* child : item->childItems()) {
      pending.push_back(child);
    }
  }
  return items;
}

QQuickItem* findItem(QQuickItem* root, const QString& name) {
  for (QQuickItem* item : visualTree(root)) {
    if (item->objectName() == name) {
      return item;
    }
  }
  return nullptr;
}

// The rawText of every visible label under `root`.
QStringList shownTexts(QQuickItem* root) {
  QStringList texts;
  for (const QQuickItem* item : visualTree(root)) {
    const QVariant text = item->property("rawText");
    if (text.isValid() && item->isVisible()) {
      texts << text.toString();
    }
  }
  return texts;
}

class HistoryViewTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
#ifndef HOLONIGHT_COMPILED_QML_ACCEPTANCE
    qmlRegisterUncreatableType<HistoryModel>("HolonightPackages", 1, 0, "HistoryModel",
                                             QStringLiteral("Provided by the test"));
    qmlRegisterType<HistoryFilterModel>("HolonightPackages", 1, 0, "HistoryFilterModel");
#endif
  }

  void SetUp() override {
    QObject::connect(&engine_, &QQmlEngine::warnings, &engine_, [](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        ADD_FAILURE() << qPrintable(error.toString());
      }
    });
  }

  void TearDown() override {
    source_->release();
    view_.reset();
  }

  // Creates the page; `active` is the workspace's "this page is shown" flag.
  QQuickItem& createView(bool active) {
    QQmlComponent component(&engine_, qmlSource(QStringLiteral("/history/HistoryView.qml")));
    EXPECT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
    view_.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties({
        {QStringLiteral("historyModel"), QVariant::fromValue(model_.get())},
        {QStringLiteral("active"), active},
    })));
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

  // Opens the page over `entries` and waits until the first read is applied.
  QQuickItem& openWith(std::vector<HistoryEntry> entries) {
    source_->enqueue(resetWith(std::move(entries)));
    QQuickItem& view = createView(true);
    EXPECT_TRUE(QTest::qWaitFor([this] { return model_->loaded() && !model_->loading(); }, kWaitMs));
    QCoreApplication::processEvents();
    return view;
  }

  [[nodiscard]] QQuickItem* item(const char* name) const { return findItem(view_.get(), QString::fromLatin1(name)); }

  [[nodiscard]] bool isVisible(const char* name) const {
    const QQuickItem* found = item(name);
    EXPECT_NE(found, nullptr) << name;
    return found != nullptr && found->isVisible();
  }

  [[nodiscard]] HistoryFilterModel* filter() const {
    auto* filter = view_->findChild<HistoryFilterModel*>();
    EXPECT_NE(filter, nullptr);
    return filter;
  }

  // The row delegates of the history list; none while the list does not exist.
  [[nodiscard]] std::vector<QQuickItem*> rows() const {
    QQuickItem* list = item("historyList");
    std::vector<QQuickItem*> result;
    if (list == nullptr) {
      return result;
    }
    const int count = list->property("count").toInt();
    for (int index = 0; index < count; ++index) {
      QQuickItem* row = nullptr;
      EXPECT_TRUE(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, index)));
      result.push_back(row);
    }
    return result;
  }

  [[nodiscard]] bool rowsReady(std::size_t expected) const {
    const auto current = rows();
    return current.size() == expected && std::ranges::all_of(current, [](const QQuickItem* row) { return row; });
  }

  // Selects `row` and waits until the detail panel shows it.
  void select(int row) {
    filter()->setCurrentRow(row);
    QCoreApplication::processEvents();
  }

  [[nodiscard]] QQuickItem* detailPanel() const { return item("historyDetailPanel"); }

  [[nodiscard]] QQuickItem& view() const { return *view_; }
  [[nodiscard]] CountingHistorySource& source() const { return *source_; }
  [[nodiscard]] HistoryModel& model() const { return *model_; }

 private:
  std::shared_ptr<CountingHistorySource> source_ = std::make_shared<CountingHistorySource>();
  std::unique_ptr<HistoryModel> model_ =
      std::make_unique<HistoryModel>(source_, HistoryModelOptions{.current_uid = kCurrentUid, .resolve_user = {}});
  QQmlEngine engine_;
  QQuickWindow window_;
  std::unique_ptr<QQuickItem> view_;
};

TEST_F(HistoryViewTest, PageOpenStartsOneFullParse) {
  source().enqueue(resetWith({historyEntry(sys_days{2026y / 11 / 23}, 1h)}));
  QQuickItem& view = createView(false);
  QCoreApplication::processEvents();
  EXPECT_EQ(source().calls(), 0U);

  view.setProperty("active", true);
  ASSERT_TRUE(QTest::qWaitFor([this] { return model().loaded() && !model().loading(); }, kWaitMs));
  view.setProperty("active", false);
  view.setProperty("active", true);
  QCoreApplication::processEvents();
  EXPECT_EQ(source().modes(), std::vector<HistoryReadMode>{HistoryReadMode::Full});
}

TEST_F(HistoryViewTest, LoadingHidesRows) {
  source().enqueue(resetWith({
      historyEntry(sys_days{2026y / 11 / 23}, 1h),
      historyEntry(sys_days{2026y / 11 / 23}, 2h),
  }));
  source().block();
  createView(true);
  source().waitForStarted(1);
  QCoreApplication::processEvents();

  EXPECT_TRUE(isVisible("historyLoadingState"));
  EXPECT_EQ(item("historyList"), nullptr);
  EXPECT_TRUE(rows().empty());
  EXPECT_FALSE(isVisible("historyEmptyState"));

  source().release();
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(2); }, kWaitMs));
  EXPECT_FALSE(isVisible("historyLoadingState"));
}

TEST_F(HistoryViewTest, EmptyStateShowsReason) {
  HistoryReadResult missing = resetWith({});
  missing.reason = "The pacman log /var/log/pacman.log does not exist.";
  source().enqueue(missing);
  createView(true);
  ASSERT_TRUE(QTest::qWaitFor([this] { return model().loaded() && !model().loading(); }, kWaitMs));
  QCoreApplication::processEvents();

  ASSERT_TRUE(isVisible("historyEmptyState"));
  EXPECT_EQ(item("historyEmptyState")->property("descriptionText").toString(),
            QStringLiteral("The pacman log /var/log/pacman.log does not exist."));
  EXPECT_FALSE(isVisible("historyLoadingState"));
  EXPECT_EQ(item("historyList"), nullptr);
  // No dialog or popup: the reason is shown inline only.
  const auto objects = view().findChildren<QObject*>();
  EXPECT_TRUE(std::ranges::none_of(objects, [](const QObject* object) { return object->inherits("QQuickPopup"); }));
}

TEST_F(HistoryViewTest, RefreshKeepsRowsAndSelection) {
  openWith(
      {historyEntry(sys_days{2026y / 11 / 22}, 1h, {"linux"}), historyEntry(sys_days{2026y / 11 / 23}, 1h, {"git"})});
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(2); }, kWaitMs));
  select(1);
  const auto sequence = filter()->currentEntry().value("sequence").value<quint32>();

  source().block();
  model().refresh();
  source().waitForStarted(2);
  QCoreApplication::processEvents();

  EXPECT_TRUE(model().loading());
  EXPECT_FALSE(isVisible("historyLoadingState"));
  EXPECT_EQ(rows().size(), 2U);
  EXPECT_EQ(filter()->currentRow(), 1);
  EXPECT_EQ(filter()->currentEntry().value("sequence").value<quint32>(), sequence);
  EXPECT_TRUE(isVisible("historyDetailTitle"));

  source().release();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !model().loading(); }, kWaitMs));
  EXPECT_EQ(filter()->currentEntry().value("sequence").value<quint32>(), sequence);
}

TEST_F(HistoryViewTest, SelectionFollowsProxyAfterAppendAndReset) {
  const auto day = sys_days{2026y / 11 / 23};
  openWith({historyEntry(day, 1h, {"linux"}), historyEntry(day, 2h, {"git"})});
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(2); }, kWaitMs));
  select(1);
  filter()->setSearchText(QStringLiteral("i"));

  HistoryReadResult delta;
  delta.append = {historyEntry(day, 3h, {"vim"})};
  source().enqueue(std::move(delta));
  model().refresh();
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3) && !model().loading(); }, kWaitMs));
  EXPECT_EQ(filter()->currentEntry().value("packageNames").toStringList(), QStringList{"linux"});
  EXPECT_EQ(filter()->currentRow(), 2);
  ASSERT_TRUE(QTest::qWaitFor([this] { return item("historyList")->property("currentIndex").toInt() == 2; }, kWaitMs));
  EXPECT_TRUE(rows()[2]->property("highlighted").toBool());
  EXPECT_FALSE(rows()[0]->property("highlighted").toBool());

  source().enqueue(
      resetWith({historyEntry(day, 4h, {"fish"}), historyEntry(day, 5h, {"ninja"}), historyEntry(day, 6h, {"curl"})}));
  model().refresh();
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(2) && !model().loading(); }, kWaitMs));
  EXPECT_EQ(filter()->currentEntry().value("packageNames").toStringList(), QStringList{"ninja"});
  EXPECT_EQ(filter()->currentRow(), 0);
  ASSERT_TRUE(QTest::qWaitFor([this] { return item("historyList")->property("currentIndex").toInt() == 0; }, kWaitMs));
  EXPECT_TRUE(rows()[0]->property("highlighted").toBool());
  EXPECT_FALSE(rows()[1]->property("highlighted").toBool());
}

TEST_F(HistoryViewTest, DetailPanelIsReachableAtMinimumWindowSize) {
  QQuickItem& page = openWith({detailedEntry()});
  page.window()->resize(720, 480);
  // The sidebar consumes 196 px plus 12 px on each side in the workspace.
  page.setSize(QSizeF(720 - 220, 480));
  QQuickItem* viewport = item("historyViewport");
  QQuickItem* toolbar = item("historyToolbar");
  QQuickItem* detail = detailPanel();
  ASSERT_NE(viewport, nullptr);
  ASSERT_NE(toolbar, nullptr);
  ASSERT_NE(detail, nullptr);
  const auto maxScroll = [viewport] { return viewport->property("contentHeight").toReal() - viewport->height(); };
  const auto detailBottom = [&] { return detail->mapToItem(viewport, QPointF(0, detail->height())).y(); };
  ASSERT_TRUE(QTest::qWaitFor(
      [&] {
        return maxScroll() > 0 && detail->height() >= 200 &&
               qAbs(detailBottom() - viewport->property("contentHeight").toReal()) < 0.5;
      },
      kWaitMs));
  const auto toolbarPosition = toolbar->mapToItem(&page, QPointF{});
  EXPECT_GT(detailBottom(), viewport->height());

  ASSERT_TRUE(viewport->setProperty("contentY", maxScroll()));
  EXPECT_GE(detail->mapToItem(viewport, QPointF{}).y(), 0);
  EXPECT_LE(detailBottom(), viewport->height() + 0.5);
  EXPECT_EQ(toolbar->mapToItem(&page, QPointF{}), toolbarPosition);
}

TEST_F(HistoryViewTest, RowShowsFourTexts) {
  const ScopedTimeZone zone("UTC0");
  HistoryEntry entry = historyEntry(sys_days{2026y / 11 / 23}, 8h + 5min + 18s, {"foo"});
  entry.origin = packagesOrigin();
  openWith({entry});
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(1); }, kWaitMs));
  QQuickItem* row = rows().front();

  const auto text = [row](const char* name) {
    const QQuickItem* label = findItem(row, QString::fromLatin1(name));
    return label != nullptr && label->isVisible() ? label->property("rawText").toString() : QString();
  };
  EXPECT_EQ(text("historyRowTime"), QStringLiteral("08:05"));
  EXPECT_EQ(text("historyRowOrigin"), QStringLiteral("Packages"));
  EXPECT_EQ(text("historyRowSummary"), QStringLiteral("1 upgraded"));
  EXPECT_EQ(text("historyRowStatus"), QStringLiteral("Completed"));
  // No column is clipped at the workspace width, in either style (REQ-C-006).
  for (const char* name : {"historyRowTime", "historyRowOrigin", "historyRowSummary", "historyRowStatus"}) {
    const QQuickItem* label = findItem(row, QString::fromLatin1(name));
    ASSERT_NE(label, nullptr) << name;
    EXPECT_FALSE(label->property("truncated").toBool()) << name;
    EXPECT_GT(label->width(), 0.0) << name;
  }
}

TEST_F(HistoryViewTest, RowsAreGroupedByDay) {
  const ScopedTimeZone zone("UTC0");
  openWith({
      historyEntry(sys_days{2026y / 11 / 22}, 1h),
      historyEntry(sys_days{2026y / 11 / 23}, 1h),
      historyEntry(sys_days{2026y / 11 / 23}, 2h),
  });
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(3); }, kWaitMs));
  int headers = 0;
  for (const QQuickItem* candidate : visualTree(item("historyList"))) {
    headers += candidate->objectName() == QStringLiteral("historyDayHeader") ? 1 : 0;
  }
  EXPECT_EQ(headers, 2);
}

TEST_F(HistoryViewTest, RefreshHandlerUsesSamePath) {
  QQuickItem& view = openWith({historyEntry(sys_days{2026y / 11 / 23}, 1h)});
  ASSERT_TRUE(QMetaObject::invokeMethod(&view, "refreshRequested"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 2 && !model().loading(); }, kWaitMs));

  // The toolbar button's own activation signal; no pointer or focus input.
  QQuickItem* button = item("historyRefreshButton");
  ASSERT_NE(button, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  ASSERT_TRUE(QTest::qWaitFor([this] { return source().calls() == 3 && !model().loading(); }, kWaitMs));
  EXPECT_EQ(source().modes(), (std::vector<HistoryReadMode>{
                                  HistoryReadMode::Full,
                                  HistoryReadMode::Incremental,
                                  HistoryReadMode::Incremental,
                              }));
}

TEST_F(HistoryViewTest, NoTransactionActions) {
  QQuickItem& view = openWith({detailedEntry(), pacmanEntry()});
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(2); }, kWaitMs));
  // Nothing on the page can reach a transaction controller or the helper client.
  for (const QQuickItem* candidate : visualTree(&view)) {
    EXPECT_FALSE(candidate->property("transactionController").isValid()) << candidate->objectName().toStdString();
    EXPECT_FALSE(candidate->property("controller").isValid()) << candidate->objectName().toStdString();
  }
  // Activating every button reads the log at most; it never starts anything else.
  int buttons = 0;
  for (QQuickItem* candidate : visualTree(&view)) {
    if (candidate->inherits("QQuickAbstractButton") && candidate->isVisible()) {
      ++buttons;
      ASSERT_TRUE(QMetaObject::invokeMethod(candidate, "clicked"));
      ASSERT_TRUE(QTest::qWaitFor([this] { return !model().loading(); }, kWaitMs));
    }
  }
  EXPECT_GE(buttons, 3);  // the refresh button and the two rows
  for (const HistoryReadMode mode : source().modes()) {
    EXPECT_TRUE(mode == HistoryReadMode::Full || mode == HistoryReadMode::Incremental);
  }
}

TEST_F(HistoryViewTest, DetailShowsActionsHooksMessages) {
  openWith({pacmanEntry(), detailedEntry()});
  ASSERT_TRUE(QTest::qWaitFor([this] { return rowsReady(2); }, kWaitMs));
  select(0);
  ASSERT_TRUE(QTest::qWaitFor([this] { return findItem(detailPanel(), "historyDetailActions") != nullptr; }, kWaitMs));

  const QStringList texts = shownTexts(detailPanel());
  EXPECT_TRUE(texts.contains(QStringLiteral("1-1 → 2-1"))) << texts.join('|').toStdString();
  EXPECT_TRUE(texts.contains(QStringLiteral("3.0-1")));
  EXPECT_TRUE(texts.contains(QStringLiteral("foo")));
  EXPECT_TRUE(texts.contains(QStringLiteral("bar")));
  EXPECT_TRUE(texts.contains(QStringLiteral("/etc/foo.conf installed as /etc/foo.conf.pacnew")));

  QQuickItem* before = findItem(detailPanel(), QStringLiteral("historyDetailPreHooks"));
  QQuickItem* after = findItem(detailPanel(), QStringLiteral("historyDetailPostHooks"));
  ASSERT_NE(before, nullptr);
  ASSERT_NE(after, nullptr);
  EXPECT_TRUE(before->isVisible());
  EXPECT_TRUE(after->isVisible());
  EXPECT_TRUE(shownTexts(before).contains(QStringLiteral("systemd-sysusers.hook")));
  EXPECT_FALSE(shownTexts(before).contains(QStringLiteral("update-desktop-database.hook")));
  EXPECT_TRUE(shownTexts(after).contains(QStringLiteral("update-desktop-database.hook")));
  EXPECT_FALSE(shownTexts(after).contains(QStringLiteral("systemd-sysusers.hook")));

  // The pacman run has no hooks and no messages: those sections are not shown.
  select(1);
  EXPECT_FALSE(findItem(detailPanel(), QStringLiteral("historyDetailPreHooks"))->isVisible());
  EXPECT_FALSE(findItem(detailPanel(), QStringLiteral("historyDetailPostHooks"))->isVisible());
  EXPECT_FALSE(findItem(detailPanel(), QStringLiteral("historyDetailMessages"))->isVisible());
}

TEST_F(HistoryViewTest, PacmanOriginShowsCommand) {
  openWith({pacmanEntry()});
  ASSERT_TRUE(QTest::qWaitFor([this] { return findItem(detailPanel(), "historyDetailCommand") != nullptr; }, kWaitMs));
  const QQuickItem* command = findItem(detailPanel(), QStringLiteral("historyDetailCommand"));
  EXPECT_TRUE(command->isVisible());
  EXPECT_EQ(command->property("rawText").toString(), QStringLiteral("pacman --sync --needed vim"));
}

TEST_F(HistoryViewTest, PackagesOriginHasNoCommandElement) {
  openWith({detailedEntry()});
  ASSERT_TRUE(QTest::qWaitFor([this] { return findItem(detailPanel(), "historyDetailActions") != nullptr; }, kWaitMs));
  EXPECT_EQ(findItem(detailPanel(), QStringLiteral("historyDetailCommand")), nullptr);
}

TEST_F(HistoryViewTest, ScriptletTextNeverShown) {
  openWith({detailedEntry()});
  ASSERT_TRUE(QTest::qWaitFor([this] { return findItem(detailPanel(), "historyDetailActions") != nullptr; }, kWaitMs));
  // The selected entry carries only the documented roles: there is no field that could hold scriptlet output.
  const QStringList keys = filter()->currentEntry().keys();
  const QSet<QString> expected{
      "sequence",   "startedAt",   "dayKey",  "timeLabel", "originKind", "originLabel", "commandLine",  "summary",
      "statusKind", "statusLabel", "actions", "preHooks",  "hooks",      "messages",    "packageNames",
  };
  EXPECT_EQ(QSet<QString>(keys.begin(), keys.end()), expected);
  // Every text of the panel comes from those fields or is a fixed label.
  const QSet<QString> fixed{
      "1 upgraded · 1 installed",
      "Started",
      "Origin",
      "Status",
      "Packages",
      "Completed",
      "foo",
      "bar",
      "upgraded",
      "installed",
      "1-1 → 2-1",
      "3.0-1",
      "Hooks before",
      "Hooks after",
      "Warnings and errors",
      "systemd-sysusers.hook",
      "update-desktop-database.hook",
      "/etc/foo.conf installed as /etc/foo.conf.pacnew",
      "1",
      "2",
  };
  for (const QString& text : shownTexts(detailPanel())) {
    if (text.contains(QStringLiteral(" at "))) {
      continue;  // the "Started" value: "<long date> at HH:MM"
    }
    EXPECT_TRUE(fixed.contains(text)) << text.toStdString();
  }
}

}  // namespace
