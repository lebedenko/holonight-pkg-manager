pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Controls
import HolonightPackages

Controls.ScrollView {
    id: root

    required property InstalledPackagesFilterModel filterModel

    // Keep the name column readable alongside the fixed metadata columns. Narrow windows
    // scroll the header and rows together instead of squeezing names out of the layout.
    readonly property int minimumTableWidth: columnSizes.minimumWidth

    PackageTableColumns { id: columnSizes }

    objectName: "installedPackageTable"
    implicitWidth: 0
    implicitHeight: 0
    contentWidth: Math.max(availableWidth, root.minimumTableWidth)
    contentHeight: availableHeight
    clip: true
    Controls.ScrollBar.horizontal: Controls.ScrollBar {}
    Controls.ScrollBar.vertical.policy: Controls.ScrollBar.AlwaysOff

    ColumnLayout {
        width: root.contentWidth
        height: root.availableHeight
        spacing: 0

        PackageTableHeader {
            columns: columnSizes
            Layout.fillWidth: true
        }

        ListView {
            id: packageList

            objectName: "packageList"
            clip: true
            visible: count > 0
            model: root.filterModel
            currentIndex: root.filterModel.currentRow
            keyNavigationEnabled: false
            Layout.fillWidth: true
            Layout.fillHeight: true
            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            // Keep keyboard selection in the same model as clicks and filter reconciliation.
            Keys.onDownPressed: {
                if (packageList.count > 0)
                    root.filterModel.currentRow = Math.min(packageList.count - 1, root.filterModel.currentRow + 1)
            }
            Keys.onUpPressed: {
                if (packageList.count > 0)
                    root.filterModel.currentRow = Math.max(0, root.filterModel.currentRow - 1)
            }

            delegate: PackageTableRow {
                id: delegate

                columns: columnSizes

                required property int index

                width: ListView.view.width
                highlighted: ListView.isCurrentItem

                onClicked: root.filterModel.currentRow = delegate.index
            }
        }

        HnEmptyState {
            objectName: "packageTableEmptyState"
            visible: packageList.count === 0
            titleText: qsTr("No packages match your filters")
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
