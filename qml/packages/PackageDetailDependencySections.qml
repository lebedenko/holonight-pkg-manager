pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    required property string description
    required property int requiredByCount
    required property var requiredByList
    required property var optionalDependencies
    required property int configFileCount
    property bool showDescription: true

    spacing: 16

    PackageDetailDescription {
        description: root.description
        visible: root.showDescription
        Layout.fillWidth: true
    }

    ColumnLayout {
        spacing: 8
        Layout.fillWidth: true

        HnSectionHeader {
            titleText: qsTr("Required by")
            dividerVisible: false
            Layout.fillWidth: true

            trailingContent: Component {
                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("%n package(s)", "", root.requiredByCount)
                    color: root.requiredByCount === 0 ? HoloniightPalette.success : HoloniightPalette.textSecondary
                }
            }
        }

        RowLayout {
            spacing: 6
            visible: root.requiredByCount === 0
            Layout.fillWidth: true

            HnStatusIndicator {
                status: HnStatusIndicator.Success
                Layout.alignment: Qt.AlignTop
            }

            HnLabel {
                role: HnTypographyRole.Body
                color: HoloniightPalette.success
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                rawText: qsTr("Safe to remove; no installed packages depend on it")
            }
        }

        ColumnLayout {
            spacing: 4
            visible: root.requiredByCount > 0
            Layout.fillWidth: true

            Repeater {
                model: root.requiredByList

                HnLabel {
                    required property string modelData

                    role: HnTypographyRole.Body
                    wrapMode: Text.WrapAnywhere
                    rawText: modelData
                    color: HoloniightPalette.textSecondary
                    Layout.fillWidth: true
                }
            }
        }
    }

    PackageChipList {
        titleText: qsTr("Optional dependencies")
        items: root.optionalDependencies
        Layout.fillWidth: true
    }

    ColumnLayout {
        spacing: 8
        Layout.fillWidth: true

        HnSectionHeader {
            titleText: qsTr("Local state")
            dividerVisible: false
            Layout.fillWidth: true

            leadingContent: HnIcon {
                source: "qrc:/qt/qml/Holonight/Controls/assets/folder.svg"
                rendering: HnIcon.Semantic
                size: 16
            }
        }

        HnLabel {
            role: HnTypographyRole.Body
            rawText: qsTr("%n configuration file(s)", "", root.configFileCount)
            color: HoloniightPalette.textSecondary
        }
    }
}
