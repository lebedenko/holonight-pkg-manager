pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Holonight as H
import Holonight.Controls
import HolonightPackages

Item {
    id: root

    required property InstalledPackagesModel installedPackagesModel

    ListView {
        id: packageList

        objectName: "packageList"
        anchors.fill: parent
        clip: true
        visible: root.installedPackagesModel.status === InstalledPackagesModel.Loaded && count > 0
        model: root.installedPackagesModel
        delegate: PackageRowDelegate {
            width: ListView.view.width
        }
        ScrollBar.vertical: H.ScrollBar {}
    }

    HnLoadingState {
        objectName: "loadingState"
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 48, 360))
        visible: root.installedPackagesModel.status === InstalledPackagesModel.Loading
        titleText: qsTr("Loading installed packages…")
    }

    HnEmptyState {
        objectName: "emptyState"
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 48, 360))
        visible: root.installedPackagesModel.status === InstalledPackagesModel.Loaded && packageList.count === 0
        titleText: qsTr("No installed packages")
    }

    HnEmptyState {
        objectName: "errorState"
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 48, 360))
        visible: root.installedPackagesModel.status === InstalledPackagesModel.Error
        titleText: qsTr("Couldn't load installed packages")
        descriptionText: root.installedPackagesModel.errorMessage
    }
}
