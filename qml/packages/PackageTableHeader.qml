pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    readonly property int checkboxColumnWidth: 40
    readonly property int originColumnWidth: 160
    readonly property int versionColumnWidth: 120
    readonly property int sizeColumnWidth: 90
    readonly property int reasonColumnWidth: 100

    implicitHeight: content.implicitHeight + 16

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 12

        CheckBox {
            objectName: "selectAllCheckBox"
            Layout.preferredWidth: root.checkboxColumnWidth
            Layout.alignment: Qt.AlignVCenter

            ToolTip.text: qsTr("Not implemented yet")
            ToolTip.visible: hovered
            ToolTip.delay: 500
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Package")
            color: HoloniightPalette.textMuted
            Layout.fillWidth: true
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Origin")
            color: HoloniightPalette.textMuted
            Layout.preferredWidth: root.originColumnWidth
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Installed Version")
            color: HoloniightPalette.textMuted
            Layout.preferredWidth: root.versionColumnWidth
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Size")
            color: HoloniightPalette.textMuted
            Layout.preferredWidth: root.sizeColumnWidth
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Reason")
            color: HoloniightPalette.textMuted
            Layout.preferredWidth: root.reasonColumnWidth
        }
    }

    HnSeparator {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
