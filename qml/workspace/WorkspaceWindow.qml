import QtQuick
import QtQuick.Layouts
import Holonight.Core
import HolonightPackages

Rectangle {
    id: root

    required property InstalledPackagesModel installedPackagesModel

    width: 1100
    height: 720
    color: HoloniightPalette.background

    Component.onCompleted: HoloniightPalette.reload()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            Layout.margins: 16
        }

        InstalledPackagesView {
            installedPackagesModel: root.installedPackagesModel
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
