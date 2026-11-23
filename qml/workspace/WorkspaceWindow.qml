import QtQuick
import QtQuick.Layouts
import Holonight.Core
import HolonightPackages
import "../explore"
import "../history"
import "../packages"
import "../transactions"
import "../updates"

Rectangle {
    id: root

    required property InstalledPackagesModel installedPackagesModel
    required property UpdatesModel updatesModel
    required property ExploreModel exploreModel
    // Optional: pending-update count published by holonight-packaged. Null, or available === false, hides the badge.
    property var updateStatusClient: null
    // Optional: the online-check view-model. Null hides the Check now control.
    property var updateCheckModel: null
    // Optional: the install/remove view-model. Null leaves the Install and Remove controls disabled and shows no dialog.
    property var transactionController: null
    // Optional: the History page model (transaction-history). Null leaves the History page empty.
    property var historyModel: null

    // Installed stays the landing page until the Updates page gains an advisor and update actions.
    property string currentPage: "installed"
    readonly property var pageIndex: ({ installed: 0, updates: 1, explore: 2, history: 3 })

    width: 1360
    height: 890
    color: HoloniightPalette.background

    Component.onCompleted: HoloniightPalette.reload()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            checkHistoryText: root.updateCheckModel ? root.updateCheckModel.checkHistoryText : qsTr("Not checked yet")
            currentPage: root.currentPage
            updatesBadgeText: root.updateStatusClient && root.updateStatusClient.available
                              && root.updateStatusClient.state === "ready"
                              && root.updateStatusClient.count > 0 ? String(root.updateStatusClient.count) : ""
            Layout.preferredWidth: 196
            Layout.fillHeight: true
            Layout.margins: 12

            onPageRequested: page => root.currentPage = page
        }

        StackLayout {
            objectName: "workspacePages"
            currentIndex: root.pageIndex[root.currentPage] ?? 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            InstalledPackagesView {
                objectName: "installedPage"
                installedPackagesModel: root.installedPackagesModel
                transactionController: root.transactionController
            }

            UpdatesView {
                objectName: "updatesPage"
                updatesModel: root.updatesModel
                updateCheckModel: root.updateCheckModel
                transactionController: root.transactionController
            }

            ExploreView {
                objectName: "explorePage"
                exploreModel: root.exploreModel
                transactionController: root.transactionController
            }

            HistoryView {
                objectName: "historyPage"
                historyModel: root.historyModel
                active: root.currentPage === "history"
            }
        }
    }

    // Inert while transactionController is null.
    TransactionDialog {
        controller: root.transactionController
    }
}
