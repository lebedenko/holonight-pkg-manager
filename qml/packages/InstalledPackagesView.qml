pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight as H
import Holonight.Controls
import HolonightPackages

Item {
    id: root

    required property InstalledPackagesModel installedPackagesModel

    readonly property bool hasPackages: root.installedPackagesModel.status === InstalledPackagesModel.Loaded
                                        && root.installedPackagesModel.totalPackageCount > 0

    InstalledPackagesFilterModel {
        id: filterModel

        // Default tab is TabFilter::Explicit via the C++ default member initializer (REQ-F-106).
        // Do not bind `tabFilter:` here declaratively -- assigning a scoped-enum literal directly
        // to a same-typed Q_PROPERTY crashes qmlcachegen's AOT compiler (Qt 6.11.2); imperative
        // assignment in a JS handler is unaffected, only the static QML binding form is.
        sourceModel: root.installedPackagesModel
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        visible: root.hasPackages

        InstalledToolbar {
            installedPackagesModel: root.installedPackagesModel
            filterModel: filterModel
            Layout.fillWidth: true
        }

        InstalledFilterTabs {
            installedPackagesModel: root.installedPackagesModel
            filterModel: filterModel
        }

        GridLayout {
            id: packageLayout

            columns: content.width >= 780 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16
            Layout.fillWidth: true
            Layout.fillHeight: true

            PackageTable {
                filterModel: filterModel
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            PackageDetailPanel {
                filterModel: filterModel
                Layout.preferredWidth: 380
                Layout.fillWidth: packageLayout.columns === 1
                Layout.fillHeight: true
            }
        }

        OrphanFooterBar {
            installedPackagesModel: root.installedPackagesModel
            Layout.fillWidth: true
        }
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
        visible: root.installedPackagesModel.status === InstalledPackagesModel.Loaded
                 && root.installedPackagesModel.totalPackageCount === 0
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
