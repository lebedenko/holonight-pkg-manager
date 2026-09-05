pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    required property var installDate
    required property string installedVersion
    required property string sizeLabel
    required property string installReason

    readonly property string installedDateText: Qt.formatDateTime(root.installDate, "MMM d, yyyy HH:mm")
    readonly property string reasonLabel: root.installReason === "explicit" ? qsTr("Explicit") : qsTr("Dependency")

    spacing: 0

    Repeater {
        model: [
            { label: qsTr("Installed"), value: root.installedDateText },
            { label: qsTr("Version"), value: root.installedVersion },
            { label: qsTr("Size"), value: root.sizeLabel },
            { label: qsTr("Reason"), value: root.reasonLabel }
        ]

        RowLayout {
            required property var modelData
            id: metadataRow

            spacing: 12
            Layout.fillWidth: true

            HnLabel {
                role: HnTypographyRole.Body
                rawText: metadataRow.modelData.label
                color: HoloniightPalette.textPrimary
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
            }

            HnLabel {
                role: HnTypographyRole.Body
                rawText: metadataRow.modelData.value
                color: HoloniightPalette.textSecondary
                wrapMode: Text.WrapAnywhere
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.topMargin: 8
                Layout.bottomMargin: 8
            }
        }
    }
}
