#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <gtest/gtest.h>
#include <memory>

namespace {

// The page entries of the sidebar, in their column order; the Settings entry sits in the footer, not in this column.
QStringList pageEntries(const QQuickItem& column) {
  QStringList names;
  for (const QQuickItem* item : column.childItems()) {
    if (item->objectName().startsWith(QStringLiteral("sidebar")) &&
        item->objectName().endsWith(QStringLiteral("Nav"))) {
      names.append(item->objectName());
    }
  }
  return names;
}

class SidebarHistoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    QObject::connect(&engine_, &QQmlEngine::warnings, &engine_, [](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        ADD_FAILURE() << qPrintable(error.toString());
      }
    });
    QQmlComponent component(&engine_,
                            QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR "/workspace/Sidebar.qml")));
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
    sidebar_.reset(qobject_cast<QQuickItem*>(component.create()));
    ASSERT_NE(sidebar_, nullptr);
    history_ = sidebar_->findChild<QQuickItem*>(QStringLiteral("sidebarHistoryNav"));
    ASSERT_NE(history_, nullptr);
  }

  [[nodiscard]] QQuickItem& sidebar() const { return *sidebar_; }
  [[nodiscard]] QQuickItem& history() const { return *history_; }

 private:
  QQmlEngine engine_;
  std::unique_ptr<QQuickItem> sidebar_;
  QQuickItem* history_ = nullptr;
};

TEST_F(SidebarHistoryTest, HistoryIsTheLastEnabledPageEntry) {
  EXPECT_TRUE(history().isEnabled());
  ASSERT_NE(history().parentItem(), nullptr);
  EXPECT_EQ(pageEntries(*history().parentItem()),
            (QStringList{"sidebarUpdatesNav", "sidebarExploreNav", "sidebarInstalledNav", "sidebarHistoryNav"}));
}

TEST_F(SidebarHistoryTest, ActivationRequestsTheHistoryPage) {
  QSignalSpy requested(&sidebar(), SIGNAL(pageRequested(QString)));
  // Emits the delegate's own activation signal; no pointer or focus input.
  ASSERT_TRUE(QMetaObject::invokeMethod(&history(), "clicked"));
  ASSERT_EQ(requested.count(), 1);
  EXPECT_EQ(requested.at(0).at(0).toString(), QStringLiteral("history"));
}

TEST_F(SidebarHistoryTest, CheckedOnlyOnTheHistoryPage) {
  EXPECT_FALSE(history().property("checked").toBool());
  sidebar().setProperty("currentPage", QStringLiteral("history"));
  EXPECT_TRUE(history().property("checked").toBool());
}

}  // namespace
