pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

// The transactions recorded in the pacman log, read-only (transaction-history DESIGN 5.8). Nothing is read until the
// page is first shown (REQ-F-017). The loading state covers only the first parse (REQ-F-024); a missing or
// unreadable log is an inline empty state with its reason and no dialog (REQ-F-021). No control here changes a
// package or asks the helper for anything (REQ-F-032).
Item {
    id: root

    objectName: "historyPage"

    // Optional HistoryModel, like transactionController elsewhere; null leaves the page empty.
    property var historyModel: null
    // True while this page is the current workspace page.
    property bool active: false

    signal refreshRequested()

    readonly property bool firstParse: root.historyModel !== null && root.historyModel.loading
                                       && !root.historyModel.loaded
    readonly property bool hasEntries: root.historyModel !== null && root.historyModel.loaded
                                       && root.historyModel.entryCount > 0
    // A refresh of an empty history keeps showing the empty state instead of a blank page.
    readonly property bool showEmpty: root.historyModel !== null && root.historyModel.loaded
                                      && root.historyModel.entryCount === 0

    function loadIfShown() {
        if (root.active && root.historyModel)
            root.historyModel.ensureLoaded()
    }

    onActiveChanged: root.loadIfShown()
    onHistoryModelChanged: root.loadIfShown()
    Component.onCompleted: root.loadIfShown()
    onRefreshRequested: {
        if (root.historyModel)
            root.historyModel.refresh()
    }

    HistoryFilterModel {
        id: historyFilter

        sourceModel: root.historyModel
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        HistoryToolbar {
            objectName: "historyToolbar"
            filterModel: historyFilter
            historyModel: root.historyModel
            Layout.fillWidth: true

            onRefreshRequested: root.refreshRequested()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: viewport

                objectName: "historyViewport"
                anchors.fill: parent
                visible: root.hasEntries
                clip: true
                contentWidth: width
                contentHeight: Math.max(height, contentLayout.minimumContentHeight)
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                Controls.ScrollBar.vertical: Controls.ScrollBar {}

                GridLayout {
                    id: contentLayout

                    readonly property real minimumContentHeight: columns === 1
                        ? historyTable.Layout.minimumHeight + detailPanel.Layout.minimumHeight + rowSpacing
                        : Math.max(historyTable.Layout.minimumHeight, detailPanel.Layout.minimumHeight)

                    width: viewport.width
                    height: viewport.contentHeight
                    columns: width >= 1092 ? 2 : 1
                    columnSpacing: 12
                    rowSpacing: 12

                    HistoryTable {
                        id: historyTable

                        filterModel: historyFilter
                        historyModel: root.historyModel
                        Layout.minimumHeight: 160
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    HistoryDetailPanel {
                        id: detailPanel

                        objectName: "historyDetailPanel"
                        filterModel: historyFilter
                        Layout.minimumHeight: 200
                        Layout.preferredWidth: 340
                        Layout.fillWidth: contentLayout.columns === 1
                        Layout.fillHeight: true
                    }
                }
            }

            HnLoadingState {
                objectName: "historyLoadingState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 360))
                visible: root.firstParse
                titleText: qsTr("Reading the pacman log…")
            }

            HnEmptyState {
                objectName: "historyEmptyState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.showEmpty
                titleText: qsTr("No transactions recorded")
                descriptionText: root.historyModel ? root.historyModel.emptyReason : ""
            }
        }
    }
}
