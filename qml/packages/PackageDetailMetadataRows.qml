pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    // List of {label, value}.
    required property var rows

    spacing: 0

    Repeater {
        model: root.rows

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
