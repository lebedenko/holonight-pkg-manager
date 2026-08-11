import QtQuick
import Holonight.Core
import HolonightPackages

Rectangle {
    id: root

    required property InstalledPackagesModel installedPackagesModel

    width: 1100
    height: 720
    color: HoloniightPalette.background

    Component.onCompleted: HoloniightPalette.reload()

    InstalledPackagesView {
        anchors.fill: parent
        installedPackagesModel: root.installedPackagesModel
    }
}
