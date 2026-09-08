pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Holonight.Core

Rectangle {
    id: root

    required property string text
    property bool emphasized: true
    property real maximumWidth: implicitWidth
    property string toolTipText: text

    readonly property color accentColor: root.emphasized ? HoloniightPalette.accentCyan : HoloniightPalette.textSecondary

    implicitWidth: label.implicitWidth + 12
    implicitHeight: label.implicitHeight + 6
    width: Math.min(implicitWidth, maximumWidth)
    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
    Controls.ToolTip.text: root.toolTipText
    Controls.ToolTip.visible: hover.hovered
    Controls.ToolTip.delay: 500
    HoverHandler { id: hover }

    radius: height / 2
    color: "transparent"
    border.color: root.accentColor
    border.width: HnMetrics.borderWidth

    HnLabel {
        id: label

        anchors.centerIn: parent
        width: Math.max(0, root.width - 12)
        elide: Text.ElideRight
        role: HnTypographyRole.Caption
        rawText: root.text
        color: root.accentColor
    }
}
