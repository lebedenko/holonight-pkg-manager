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

    HnSettingsRow {
        titleText: qsTr("Installed")
        Layout.fillWidth: true

        trailingContent: Component {
            HnLabel {
                role: HnTypographyRole.Body
                rawText: root.installedDateText
                color: HoloniightPalette.textSecondary
            }
        }
    }

    HnSettingsRow {
        titleText: qsTr("Version")
        Layout.fillWidth: true

        trailingContent: Component {
            HnLabel {
                role: HnTypographyRole.Body
                rawText: root.installedVersion
                color: HoloniightPalette.textSecondary
            }
        }
    }

    HnSettingsRow {
        titleText: qsTr("Size")
        Layout.fillWidth: true

        trailingContent: Component {
            HnLabel {
                role: HnTypographyRole.Body
                rawText: root.sizeLabel
                color: HoloniightPalette.textSecondary
            }
        }
    }

    HnSettingsRow {
        titleText: qsTr("Reason")
        Layout.fillWidth: true

        trailingContent: Component {
            HnLabel {
                role: HnTypographyRole.Body
                rawText: root.reasonLabel
                color: HoloniightPalette.textSecondary
            }
        }
    }
}
