pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// Titled list of chips with a count and a "+N more" expander.
ColumnLayout {
    id: root

    required property string titleText
    required property var items

    property int visibleLimit: 5
    property bool hideWhenEmpty: false
    property bool expanded: false

    spacing: 8
    visible: !root.hideWhenEmpty || root.items.length > 0

    HnSectionHeader {
        titleText: root.titleText
        dividerVisible: false
        Layout.fillWidth: true

        trailingContent: Component {
            HnLabel {
                role: HnTypographyRole.Caption
                rawText: String(root.items.length)
                color: HoloniightPalette.textSecondary
            }
        }
    }

    Flow {
        id: chips

        spacing: 8
        Layout.fillWidth: true

        Repeater {
            model: root.expanded ? root.items : root.items.slice(0, root.visibleLimit)

            Rectangle {
                required property string modelData

                width: Math.min(implicitWidth, chips.width)
                implicitWidth: chipLabel.implicitWidth + 16
                implicitHeight: chipLabel.height + 8
                radius: height / 2
                color: HoloniightPalette.surfaceElevated

                HnLabel {
                    id: chipLabel

                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - 16)
                    wrapMode: Text.WrapAnywhere
                    role: HnTypographyRole.Caption
                    rawText: parent.modelData
                    color: HoloniightPalette.textSecondary
                }
            }
        }

        Controls.Button {
            id: moreButton

            visible: !root.expanded && root.items.length > root.visibleLimit
            text: qsTr("+%1 more").arg(root.items.length - root.visibleLimit)
            focusPolicy: Qt.StrongFocus
            padding: 4
            leftPadding: 8
            rightPadding: 8
            Accessible.name: moreButton.text

            contentItem: HnLabel {
                role: HnTypographyRole.Caption
                rawText: moreButton.text
                color: HoloniightPalette.textSecondary
            }

            background: Rectangle {
                radius: height / 2
                color: moreButton.down ? HoloniightPalette.surfaceElevated : "transparent"
                border.color: moreButton.visualFocus ? HoloniightPalette.borderFocus : HoloniightPalette.borderPassive
                border.width: moreButton.visualFocus ? HnMetrics.focusBorderWidth : HnMetrics.borderWidth
            }

            onClicked: root.expanded = true
        }
    }
}
