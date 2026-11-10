pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property string packageName
    required property url iconSource

    implicitWidth: 24
    implicitHeight: 24
    Accessible.ignored: true

    Rectangle {
        anchors.fill: parent
        visible: image.status !== Image.Ready
        radius: 8
        color: HoloniightPalette.surfaceElevated

        HnLabel {
            anchors.centerIn: parent
            role: HnTypographyRole.Body
            rawText: root.packageName.length > 0 ? root.packageName.charAt(0).toUpperCase() : "?"
            color: HoloniightPalette.textSecondary
        }
    }

    Image {
        id: image

        anchors.fill: parent
        source: root.iconSource
        sourceSize: Qt.size(64, 64)
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        visible: status === Image.Ready
    }
}
