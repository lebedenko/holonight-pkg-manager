import QtQuick
import Holonight.Core

Rectangle {
    width: 1100
    height: 720
    color: HoloniightPalette.background

    Component.onCompleted: HoloniightPalette.reload()

    Text {
        anchors.centerIn: parent
        color: HoloniightPalette.textPrimary
        font.pixelSize: 18
        text: qsTr("HoloNight Packages — under construction")
    }
}
