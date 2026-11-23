pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

// Transactions newest first, grouped by local day (REQ-F-025). The list exists only once a read has finished with
// entries: during the first parse there is no row (REQ-F-024), and a later refresh keeps the list, its rows and its
// selection. Header and rows scroll horizontally together on narrow windows, as in ExploreTable.qml.
Controls.ScrollView {
    id: root

    required property HistoryFilterModel filterModel
    property var historyModel: null

    readonly property bool hasEntries: root.historyModel !== null && root.historyModel.loaded
                                       && root.historyModel.entryCount > 0

    HistoryColumns { id: columnSizes }

    objectName: "historyTable"
    implicitWidth: 0
    implicitHeight: 0
    contentWidth: Math.max(availableWidth, columnSizes.minimumWidth)
    contentHeight: availableHeight
    clip: true
    Controls.ScrollBar.horizontal: Controls.ScrollBar {}
    Controls.ScrollBar.vertical.policy: Controls.ScrollBar.AlwaysOff

    ColumnLayout {
        width: root.contentWidth
        height: root.availableHeight
        spacing: 0

        Item {
            implicitHeight: header.implicitHeight + 16
            Layout.fillWidth: true

            RowLayout {
                id: header

                anchors.fill: parent
                anchors.leftMargin: columnSizes.padding
                anchors.rightMargin: columnSizes.padding
                spacing: columnSizes.spacing

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Time")
                    color: HoloniightPalette.textMuted
                    Layout.minimumWidth: columnSizes.time
                    Layout.maximumWidth: columnSizes.time
                    Layout.preferredWidth: columnSizes.time
                }

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Origin")
                    color: HoloniightPalette.textMuted
                    Layout.minimumWidth: columnSizes.origin
                    Layout.maximumWidth: columnSizes.origin
                    Layout.preferredWidth: columnSizes.origin
                }

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Changes")
                    color: HoloniightPalette.textMuted
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                }

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Status")
                    color: HoloniightPalette.textMuted
                    horizontalAlignment: Text.AlignRight
                    Layout.minimumWidth: columnSizes.status
                    Layout.maximumWidth: columnSizes.status
                    Layout.preferredWidth: columnSizes.status
                }
            }

            HnSeparator {
                crossAxisAlignment: HnSeparator.Trailing
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                objectName: "historyTableLoader"
                anchors.fill: parent
                active: root.hasEntries
                sourceComponent: listComponent
            }

            HnEmptyState {
                objectName: "historyNoMatchState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 360))
                visible: root.hasEntries && root.filterModel.searchText.length > 0 && root.filterModel.count === 0
                titleText: qsTr("No transactions match")
                descriptionText: qsTr("No transaction changed a package whose name contains “%1”.")
                                     .arg(root.filterModel.searchText)
            }
        }
    }

    Component {
        id: listComponent

        ListView {
            id: historyList

            objectName: "historyList"
            clip: true
            model: root.filterModel
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            section.property: "dayKey"
            section.criteria: ViewSection.FullString
            section.delegate: HistoryDayHeader {
                width: ListView.view.width
                historyModel: root.historyModel
            }
            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            function syncCurrentIndex() {
                historyList.forceLayout()
                historyList.currentIndex = root.filterModel.currentRow
            }

            Component.onCompleted: Qt.callLater(historyList.syncCurrentIndex)

            Connections {
                target: root.filterModel

                function onCurrentRowChanged() { Qt.callLater(historyList.syncCurrentIndex) }
                function onRowsInserted() { Qt.callLater(historyList.syncCurrentIndex) }
                function onRowsRemoved() { Qt.callLater(historyList.syncCurrentIndex) }
                function onModelReset() { Qt.callLater(historyList.syncCurrentIndex) }
            }

            // Keep keyboard selection in the same model as clicks.
            Keys.onDownPressed: {
                if (historyList.count > 0)
                    root.filterModel.currentRow = Math.min(historyList.count - 1, root.filterModel.currentRow + 1)
            }
            Keys.onUpPressed: {
                if (historyList.count > 0)
                    root.filterModel.currentRow = Math.max(0, root.filterModel.currentRow - 1)
            }

            delegate: HistoryRow {
                id: delegate

                columns: columnSizes

                required property int index

                width: ListView.view.width
                highlighted: delegate.index === root.filterModel.currentRow

                onClicked: root.filterModel.currentRow = delegate.index
            }
        }
    }
}
