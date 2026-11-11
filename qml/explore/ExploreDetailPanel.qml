pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Controls
import HolonightPackages
import "../packages"

// Details of the selected search result. Composes only the shared detail components that make sense for a package
// that is not (necessarily) installed: a single Install action, no local-state sections.
PackageDetailFrame {
    id: root

    required property ExploreModel exploreModel
    // Optional TransactionController; null leaves the Install control disabled.
    property var transactionController: null

    hasSelection: root.exploreModel.currentRow >= 0
    detailContent: detailContentComponent
    // A fully-keyed fallback so bindings never see `undefined` while the Loader deactivates.
    readonly property var emptyPackage: ({
        name: "", availableVersion: "", repository: "", description: "", downloadSizeLabel: "",
        isInstalled: false, installedSizeLabel: "", url: "", licenses: [], dependencies: [], optionalDependencies: [],
    })
    readonly property var currentPackage: root.hasSelection ? root.exploreModel.currentPackage : root.emptyPackage
    readonly property var metadataRows: [
        { label: qsTr("Version"), value: root.currentPackage.availableVersion },
        { label: qsTr("Download size"), value: root.currentPackage.downloadSizeLabel },
        { label: qsTr("Installed size"), value: root.currentPackage.installedSizeLabel },
        { label: qsTr("License"), value: root.currentPackage.licenses.join(", ") },
        { label: qsTr("URL"), value: root.currentPackage.url }
    ].filter(row => row.value.length > 0)

    Component {
        id: detailContentComponent

        PackageDetailContent {
            name: root.currentPackage.name
            sourceLabel: "repository"
            repository: root.currentPackage.repository
            metadataRows: root.metadataRows
            description: root.currentPackage.description
            showActions: true
            actionKind: "install"
            showMoreOptions: false
            transactionController: root.transactionController
            showInstalledIndicator: root.currentPackage.isInstalled
            extraSections: Component {
                ColumnLayout {
                    spacing: 16

                    PackageChipList {
                        titleText: qsTr("Dependencies")
                        items: root.currentPackage.dependencies
                        hideWhenEmpty: true
                        Layout.fillWidth: true
                    }

                    PackageChipList {
                        titleText: qsTr("Optional dependencies")
                        items: root.currentPackage.optionalDependencies
                        hideWhenEmpty: true
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
