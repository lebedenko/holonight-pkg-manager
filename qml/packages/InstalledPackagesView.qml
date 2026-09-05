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

    ScrollView {
        id: pageScroll

        objectName: "installedPageScrollView"
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth
        contentHeight: content.implicitHeight
        visible: root.hasPackages
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: H.ScrollBar {}

        Column {
            id: content

            width: pageScroll.availableWidth
            spacing: 16

            InstalledToolbar {
                id: toolbar

                width: content.width
                installedPackagesModel: root.installedPackagesModel
                filterModel: filterModel
            }

            InstalledFilterTabs {
                id: filterTabs

                width: content.width
                installedPackagesModel: root.installedPackagesModel
                filterModel: filterModel
            }

            GridLayout {
                id: packageLayout

                readonly property real minimumContentHeight: columns === 1
                    ? packageTable.Layout.minimumHeight + packageDetail.Layout.minimumHeight + rowSpacing
                    : Math.max(packageTable.Layout.minimumHeight, packageDetail.Layout.minimumHeight)

                width: content.width
                // Allocate the remaining viewport space without reading this layout's own size hints.
                height: Math.max(minimumContentHeight, pageScroll.availableHeight
                    - toolbar.height - filterTabs.height - footer.height - 3 * content.spacing)
                objectName: "installedPackageLayout"
                columns: content.width >= 1092 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                PackageTable {
                    id: packageTable

                    filterModel: filterModel
                    Layout.minimumHeight: 160
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                PackageDetailPanel {
                    id: packageDetail

                    filterModel: filterModel
                    Layout.minimumHeight: 200
                    objectName: "packageDetailPanel"
                    Layout.preferredWidth: 300
                    Layout.fillWidth: packageLayout.columns === 1
                    Layout.fillHeight: true
                }
            }

            OrphanFooterBar {
                id: footer

                width: content.width
                installedPackagesModel: root.installedPackagesModel
            }
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
