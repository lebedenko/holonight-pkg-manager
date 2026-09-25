pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Controls
import HolonightPackages

PackageDetailFrame {
    id: root

    required property InstalledPackagesFilterModel filterModel

    hasSelection: root.filterModel.currentRow >= 0
    detailContent: detailContentComponent
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
    readonly property var metadataRows: [
        { label: qsTr("Installed"), value: Qt.formatDateTime(root.currentPackage.installDate, "MMM d, yyyy HH:mm") },
        { label: qsTr("Version"), value: root.currentPackage.installedVersion },
        { label: qsTr("Size"), value: root.currentPackage.sizeLabel },
        { label: qsTr("Reason"), value: root.currentPackage.installReason === "explicit" ? qsTr("Explicit")
                                                                                         : qsTr("Dependency") }
    ]

    Component {
        id: detailContentComponent

        PackageDetailContent {
            name: root.currentPackage.name
            sourceLabel: root.currentPackage.sourceLabel
            repository: root.currentPackage.repository
            metadataRows: root.metadataRows
            description: root.currentPackage.description
            extraSections: Component {
                PackageDetailDependencySections {
                    description: root.currentPackage.description
                    showDescription: false
                    requiredByCount: root.currentPackage.requiredByCount
                    requiredByList: root.currentPackage.requiredByList
                    optionalDependencies: root.currentPackage.optionalDependencies
                    configFileCount: root.currentPackage.configFileCount
                }
            }

            footerContent: Component {
                PackageDetailFooterLinks {}
            }
        }
    }
}
