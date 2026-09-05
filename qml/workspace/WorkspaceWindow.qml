import QtQuick
import QtQuick.Layouts
import Holonight.Core
import HolonightPackages
import "../packages"

Rectangle {
    id: root

    required property InstalledPackagesModel installedPackagesModel

    width: 1360
    height: 890
    color: HoloniightPalette.background

    Component.onCompleted: HoloniightPalette.reload()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.preferredWidth: 196
            Layout.fillHeight: true
            Layout.margins: 12
        }

        InstalledPackagesView {
            installedPackagesModel: root.installedPackagesModel
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
