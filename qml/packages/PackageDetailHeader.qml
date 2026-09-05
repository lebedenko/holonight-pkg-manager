pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
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
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            RowLayout {
                spacing: 8

                PackageOriginBadge {
                    text: root.sourceLabel === "official" ? qsTr("Official") : qsTr("AUR")
                    emphasized: true
                }

                PackageOriginBadge {
                    text: root.repository
                    emphasized: false
                    visible: root.repository.length > 0
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

        Button {
            id: removeButton

            objectName: "packageDetailRemoveButton"
            Layout.fillWidth: true

            ToolTip.text: qsTr("Not implemented yet")
            ToolTip.visible: hovered
            ToolTip.delay: 500

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

            ToolTip.text: qsTr("Not implemented yet")
            ToolTip.visible: hovered
            ToolTip.delay: 500
        }
    }

    HnSeparator {
        Layout.fillWidth: true
    }
}
