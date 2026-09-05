pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight as H
import Holonight.Core
import Holonight.Controls
import HolonightPackages

ColumnLayout {
    id: root

    required property InstalledPackagesModel installedPackagesModel
    required property InstalledPackagesFilterModel filterModel

    readonly property var sortOptions: [
        { text: qsTr("Name A → Z"), field: InstalledPackagesFilterModel.Name, descending: false },
        { text: qsTr("Name Z → A"), field: InstalledPackagesFilterModel.Name, descending: true },
        { text: qsTr("Size Large → Small"), field: InstalledPackagesFilterModel.Size, descending: true },
        { text: qsTr("Size Small → Large"), field: InstalledPackagesFilterModel.Size, descending: false }
    ]
    readonly property int currentSortIndex: root.sortOptions.findIndex(option =>
        option.field === root.filterModel.sortField && option.descending === root.filterModel.sortDescending)
    readonly property bool compact: root.width < 780

    spacing: 12

    GridLayout {
        columns: root.compact ? 4 : 6
        columnSpacing: 16
        rowSpacing: 12
        Layout.fillWidth: true

        ColumnLayout {
            spacing: 2
            Layout.columnSpan: root.compact ? 4 : 1

            HnLabel {
                role: HnTypographyRole.Heading
                font.bold: true
                rawText: qsTr("Installed")
                color: HoloniightPalette.textPrimary
            }

            HnLabel {
                objectName: "installedToolbarSubtitle"
                role: HnTypographyRole.Caption
                rawText: qsTr("%1 packages · %2").arg(root.installedPackagesModel.totalPackageCount)
                                                  .arg(root.installedPackagesModel.formatSize(
                                                      root.installedPackagesModel.totalInstalledSizeBytes))
                color: HoloniightPalette.textMuted
            }
        }

        Item {
            visible: !root.compact
            Layout.fillWidth: true
        }

        HnSearchField {
            objectName: "installedSearchField"
            placeholderText: qsTr("Search installed packages")
            text: root.filterModel.searchText
            Layout.minimumWidth: 100
            Layout.preferredWidth: 320
            Layout.fillWidth: root.compact

            onTextChanged: root.filterModel.searchText = text
        }

        H.ComboBox {
            id: sortComboBox

            objectName: "installedSortComboBox"
            textRole: "text"
            model: root.sortOptions
            currentIndex: root.currentSortIndex

            onActivated: index => {
                root.filterModel.sortField = root.sortOptions[index].field
                root.filterModel.sortDescending = root.sortOptions[index].descending
            }
        }

        Row {
            spacing: 2

            HnIconButton {
                id: listViewButton

                objectName: "installedListViewButton"
                checkable: true
                checked: true
                autoExclusive: true

                contentItem: HnLabel {
                    anchors.centerIn: parent
                    role: HnTypographyRole.Body
                    rawText: "☰"
                    color: listViewButton.checked ? HoloniightPalette.textPrimary : HoloniightPalette.textMuted
                }
            }

            HnIconButton {
                id: gridViewButton

                objectName: "installedGridViewButton"
                checkable: true
                autoExclusive: true

                ToolTip.text: qsTr("Grid view is not implemented yet")
                ToolTip.visible: hovered
                ToolTip.delay: 500

                contentItem: HnLabel {
                    anchors.centerIn: parent
                    role: HnTypographyRole.Body
                    rawText: "⊞"
                    color: gridViewButton.checked ? HoloniightPalette.textPrimary : HoloniightPalette.textMuted
                }
            }
        }

        HnIconButton {
            objectName: "installedOverflowButton"
            icon.source: "qrc:/qt/qml/Holonight/Controls/assets/more-vertical.svg"

            ToolTip.text: qsTr("Not implemented yet")
            ToolTip.visible: hovered
            ToolTip.delay: 500
        }
    }

    RowLayout {
        spacing: 12
        Layout.fillWidth: true

        Item {
            Layout.fillWidth: true
        }

        H.ComboBox {
            objectName: "installedRepositoryComboBox"
            model: [qsTr("All repositories")].concat(root.filterModel.availableRepositories)
            currentIndex: root.filterModel.repositoryFilter.length === 0
                          ? 0
                          : root.filterModel.availableRepositories.indexOf(root.filterModel.repositoryFilter) + 1

            onActivated: index => {
                root.filterModel.repositoryFilter = index === 0 ? "" : root.filterModel.availableRepositories[index - 1]
            }
        }

        H.ComboBox {
            objectName: "installedAllStatesComboBox"
            model: [qsTr("All states")]
            currentIndex: 0
        }
    }
}
