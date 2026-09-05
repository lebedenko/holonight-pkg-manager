pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

Row {
    id: root

    required property InstalledPackagesModel installedPackagesModel
    required property InstalledPackagesFilterModel filterModel

    spacing: 8

    Repeater {
        model: [
            {
                tab: InstalledPackagesFilterModel.Explicit,
                label: qsTr("Explicit"),
                count: root.installedPackagesModel.explicitPackageCount,
                dot: false
            },
            {
                tab: InstalledPackagesFilterModel.Dependencies,
                label: qsTr("Dependencies"),
                count: root.installedPackagesModel.dependencyPackageCount,
                dot: false
            },
            {
                tab: InstalledPackagesFilterModel.Foreign,
                label: qsTr("AUR / Foreign"),
                count: root.installedPackagesModel.foreignPackageCount,
                dot: false
            },
            {
                tab: InstalledPackagesFilterModel.Orphans,
                label: qsTr("Orphans"),
                count: root.installedPackagesModel.orphanPackageCount,
                dot: true
            }
        ]

        // HnListDelegate rather than the internal HnSelectableDelegate it wraps -- see
        // PackageTableRow.qml for why.
        HnListDelegate {
            id: tabDelegate

            required property var modelData

            selectionStyle: HnListDelegate.Fill
            checked: root.filterModel.tabFilter === tabDelegate.modelData.tab

            contentItem: RowLayout {
                spacing: 6

                Rectangle {
                    visible: tabDelegate.modelData.dot
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    Layout.alignment: Qt.AlignVCenter
                    radius: 3.5
                    color: HoloniightPalette.warning
                }

                HnLabel {
                    role: HnTypographyRole.Body
                    rawText: tabDelegate.modelData.label
                    color: tabDelegate.checked ? HoloniightPalette.textPrimary : HoloniightPalette.textSecondary
                }

                Rectangle {
                    implicitWidth: countLabel.implicitWidth + 12
                    implicitHeight: countLabel.implicitHeight + 4
                    radius: height / 2
                    color: HoloniightPalette.surfaceElevated

                    HnLabel {
                        id: countLabel

                        anchors.centerIn: parent
                        role: HnTypographyRole.Caption
                        rawText: String(tabDelegate.modelData.count)
                        color: HoloniightPalette.textSecondary
                    }
                }
            }

            onClicked: root.filterModel.tabFilter = tabDelegate.modelData.tab
        }
    }
}
