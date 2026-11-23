pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

// Title, package-name filter (REQ-F-029) and the manual refresh (REQ-F-031). Placed directly in the page column,
// never under a Layout.preferred* size, so a ColumnLayout root is safe here.
ColumnLayout {
    id: root

    required property HistoryFilterModel filterModel
    property var historyModel: null

    signal refreshRequested()

    spacing: 12

    RowLayout {
        spacing: 12
        Layout.fillWidth: true

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true

            HnLabel {
                role: HnTypographyRole.Heading
                font.bold: true
                rawText: qsTr("History")
                color: HoloniightPalette.textPrimary
            }

            HnLabel {
                objectName: "historyEntryCount"
                role: HnTypographyRole.Caption
                rawText: root.historyModel && root.historyModel.loaded && root.historyModel.entryCount > 0
                         ? qsTr("%n transaction(s) in the pacman log", "", root.historyModel.entryCount) : ""
                visible: text.length > 0
                color: HoloniightPalette.textMuted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        HnIconButton {
            id: refreshButton

            objectName: "historyRefreshButton"
            enabled: root.historyModel !== null
            Accessible.name: qsTr("Refresh history")
            Layout.alignment: Qt.AlignTop
            Controls.ToolTip.text: qsTr("Read new entries of the pacman log")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 500

            contentItem: HnLabel {
                anchors.centerIn: parent
                role: HnTypographyRole.Body
                rawText: "↻"
                color: refreshButton.enabled ? HoloniightPalette.textPrimary : HoloniightPalette.textMuted
            }

            onClicked: root.refreshRequested()
        }
    }

    HnSearchField {
        objectName: "historySearchField"
        placeholderText: qsTr("Search packages in history")
        text: root.filterModel.searchText
        Layout.fillWidth: true

        onTextChanged: root.filterModel.searchText = text
    }
}
