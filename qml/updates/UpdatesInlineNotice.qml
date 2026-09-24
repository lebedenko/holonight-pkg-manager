pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// Non-blocking inline message; never takes focus or blocks interaction with the page.
Rectangle {
    id: root

    required property string text
    property int status: HnStatusIndicator.Info

    implicitHeight: content.implicitHeight + 16
    radius: 8
    color: HoloniightPalette.surfaceElevated
    border.color: root.status === HnStatusIndicator.Error ? HoloniightPalette.borderUrgent : HoloniightPalette.borderPassive
    border.width: HnMetrics.borderWidth
    Accessible.role: Accessible.AlertMessage
    Accessible.name: root.text

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        HnStatusIndicator {
            status: root.status
            Layout.alignment: Qt.AlignTop
        }

        HnLabel {
            objectName: "inlineNoticeText"
            role: HnTypographyRole.Body
            rawText: root.text
            wrapMode: Text.Wrap
            color: HoloniightPalette.textPrimary
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }
    }
}
