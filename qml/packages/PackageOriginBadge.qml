pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Core

Rectangle {
    id: root

    required property string text
    property bool emphasized: true

    readonly property color accentColor: root.emphasized ? HoloniightPalette.accentCyan : HoloniightPalette.textSecondary

    implicitWidth: label.implicitWidth + 16
    implicitHeight: label.implicitHeight + 8
    radius: height / 2
    color: "transparent"
    border.color: root.accentColor
    border.width: HnMetrics.borderWidth

    HnLabel {
        id: label

        anchors.centerIn: parent
        role: HnTypographyRole.Caption
        rawText: root.text
        color: root.accentColor
    }
}
