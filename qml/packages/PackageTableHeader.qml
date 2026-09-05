pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property PackageTableColumns columns

    implicitHeight: content.implicitHeight + 16

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.leftMargin: root.columns.padding
        anchors.rightMargin: root.columns.padding
        spacing: root.columns.spacing

        CheckBox {
            objectName: "selectAllCheckBox"
            Layout.minimumWidth: root.columns.checkbox
            Layout.maximumWidth: root.columns.checkbox
            Layout.preferredWidth: root.columns.checkbox
            Layout.alignment: Qt.AlignVCenter

            ToolTip.text: qsTr("Not implemented yet")
            ToolTip.visible: hovered
            ToolTip.delay: 500
        }

        Item {
            Layout.preferredWidth: root.columns.icon
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
            Layout.minimumWidth: root.columns.origin
            Layout.maximumWidth: root.columns.origin
            Layout.preferredWidth: root.columns.origin
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Version")
            color: HoloniightPalette.textMuted
            Layout.minimumWidth: root.columns.version
            Layout.maximumWidth: root.columns.version
            Layout.preferredWidth: root.columns.version
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Size")
            color: HoloniightPalette.textMuted
            Layout.minimumWidth: root.columns.size
            Layout.maximumWidth: root.columns.size
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: root.columns.size
        }

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Reason")
            color: HoloniightPalette.textMuted
            Layout.minimumWidth: root.columns.reason
            Layout.maximumWidth: root.columns.reason
            Layout.preferredWidth: root.columns.reason
        }
    }

    HnSeparator {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
