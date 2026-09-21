pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    required property string name
    required property string sourceLabel
    required property string repository

    spacing: 12

    RowLayout {
        spacing: 12
        Layout.fillWidth: true

        Rectangle {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            Layout.alignment: Qt.AlignTop
            radius: 10
            color: HoloniightPalette.surfaceElevated

            HnLabel {
                anchors.centerIn: parent
                role: HnTypographyRole.Title
                rawText: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
                color: HoloniightPalette.textSecondary
            }
        }

        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true

            HnLabel {
                role: HnTypographyRole.Title
                font.bold: true
                rawText: root.name
                color: HoloniightPalette.textPrimary
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }

            Flow {
                id: badges

                spacing: 8
                Layout.fillWidth: true

                PackageOriginBadge {
                    objectName: "packageDetailOriginBadge"
                    text: root.sourceLabel === "official"
                        ? (root.repository.length > 0 ? root.repository : qsTr("Official")) : qsTr("Foreign")
                    emphasized: root.sourceLabel === "official"
                    maximumWidth: badges.width
                    toolTipText: root.sourceLabel === "official" && root.repository.length > 0
                        ? qsTr("Repository: %1").arg(root.repository) : text
                }

                HnStatusIndicator {
                    status: HnStatusIndicator.Success
                    text: qsTr("Installed")
                }
            }
        }
    }

    RowLayout {
        spacing: 8
        Layout.fillWidth: true

        Controls.Button {
            id: removeButton

            objectName: "packageDetailRemoveButton"
            Layout.fillWidth: true

            Controls.ToolTip.text: qsTr("Not implemented yet")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 500

            background: Rectangle {
                radius: 8
                color: "transparent"
                border.color: HoloniightPalette.error
                border.width: HnMetrics.borderWidth
            }

            contentItem: RowLayout {
                spacing: 6

                Item {
                    Layout.fillWidth: true
                }

                HnIcon {
                    source: "qrc:/qt/qml/Holonight/Controls/assets/delete.svg"
                    size: 16
                    normalColor: HoloniightPalette.error
                }

                HnLabel {
                    role: HnTypographyRole.Body
                    rawText: qsTr("Remove")
                    color: HoloniightPalette.error
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        HnIconButton {
            objectName: "packageDetailMoreOptionsButton"

            icon.source: "qrc:/qt/qml/Holonight/Controls/assets/more-vertical.svg"

            Controls.ToolTip.text: qsTr("Not implemented yet")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 500
        }
    }

    HnSeparator {
        crossAxisAlignment: HnSeparator.Trailing
        Layout.fillWidth: true
    }
}
