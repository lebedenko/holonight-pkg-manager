pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight as H
import Holonight.Core
import Holonight.Controls
import HolonightPackages

HnSurfaceFrame {
    id: root

    required property InstalledPackagesFilterModel filterModel

    surfaceRole: HnSurfaceRole.Panel

    readonly property bool hasSelection: root.filterModel.currentRow >= 0
    // A fully-keyed fallback (not just `{}`) so that PackageDetailHeader/MetadataRows/DependencySections'
    // bindings never see `undefined` for a role -- including during the Loader's activate/deactivate
    // transition, where these bindings can still evaluate once more against the just-cleared value.
    readonly property var emptyPackage: ({
        name: "", sourceLabel: "", repository: "", installDate: new Date(0), installedVersion: "",
        sizeLabel: "", installReason: "explicit", description: "", requiredByCount: 0, requiredByList: [],
        optionalDependencies: [], configFileCount: 0,
    })
    readonly property var currentPackage: root.hasSelection ? root.filterModel.currentPackage
                                                             : root.emptyPackage

    HnEmptyState {
        objectName: "packageDetailEmptyState"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 280)
        visible: !root.hasSelection
        titleText: qsTr("Select a package to view details")
    }

    ScrollView {
        id: detailScroll
        objectName: "packageDetailScrollView"
        anchors.fill: parent
        visible: root.hasSelection
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: H.ScrollBar {}
        clip: true

        Loader {
            width: detailScroll.availableWidth
            active: root.hasSelection
            sourceComponent: detailContent
        }
    }

    Component {
        id: detailContent

        ColumnLayout {
            spacing: 16

            PackageDetailHeader {
                name: root.currentPackage.name
                sourceLabel: root.currentPackage.sourceLabel
                repository: root.currentPackage.repository
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 16
            }

            PackageDetailMetadataRows {
                installDate: root.currentPackage.installDate
                installedVersion: root.currentPackage.installedVersion
                sizeLabel: root.currentPackage.sizeLabel
                installReason: root.currentPackage.installReason
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
            }

            PackageDetailDependencySections {
                description: root.currentPackage.description
                requiredByCount: root.currentPackage.requiredByCount
                requiredByList: root.currentPackage.requiredByList
                optionalDependencies: root.currentPackage.optionalDependencies
                configFileCount: root.currentPackage.configFileCount
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
            }

            PackageDetailFooterLinks {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
            }
        }
    }
}
