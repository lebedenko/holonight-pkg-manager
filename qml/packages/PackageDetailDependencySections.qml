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

    readonly property int optionalDependenciesVisibleLimit: 5

    property bool optionalDependenciesExpanded: false

    spacing: 16

    HnLabel {
        role: HnTypographyRole.Body
        rawText: root.description
        color: HoloniightPalette.textSecondary
        wrapMode: Text.WordWrap
        visible: text.length > 0
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

        HnStatusIndicator {
            status: HnStatusIndicator.Success
            text: qsTr("Safe to remove; no installed packages depend on it")
            visible: root.requiredByCount === 0
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
                    rawText: modelData
                    color: HoloniightPalette.textSecondary
                    Layout.fillWidth: true
                }
            }
        }
    }

    ColumnLayout {
        spacing: 8
        Layout.fillWidth: true

        HnSectionHeader {
            titleText: qsTr("Optional dependencies")
            dividerVisible: false
            Layout.fillWidth: true

            trailingContent: Component {
                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: String(root.optionalDependencies.length)
                    color: HoloniightPalette.textSecondary
                }
            }
        }

        Flow {
            spacing: 8
            Layout.fillWidth: true

            Repeater {
                model: root.optionalDependenciesExpanded
                       ? root.optionalDependencies
                       : root.optionalDependencies.slice(0, root.optionalDependenciesVisibleLimit)

                Rectangle {
                    required property string modelData

                    implicitWidth: chipLabel.implicitWidth + 16
                    implicitHeight: chipLabel.implicitHeight + 8
                    radius: height / 2
                    color: HoloniightPalette.surfaceElevated

                    HnLabel {
                        id: chipLabel

                        anchors.centerIn: parent
                        role: HnTypographyRole.Caption
                        rawText: parent.modelData
                        color: HoloniightPalette.textSecondary
                    }
                }
            }

            Rectangle {
                visible: !root.optionalDependenciesExpanded
                         && root.optionalDependencies.length > root.optionalDependenciesVisibleLimit
                implicitWidth: moreLabel.implicitWidth + 16
                implicitHeight: moreLabel.implicitHeight + 8
                radius: height / 2
                color: "transparent"
                border.color: HoloniightPalette.borderPassive
                border.width: HnMetrics.borderWidth

                HnLabel {
                    id: moreLabel

                    anchors.centerIn: parent
                    role: HnTypographyRole.Caption
                    rawText: qsTr("+%1 more").arg(root.optionalDependencies.length
                                                   - root.optionalDependenciesVisibleLimit)
                    color: HoloniightPalette.textSecondary
                }

                TapHandler {
                    onTapped: root.optionalDependenciesExpanded = true
                }
            }
        }
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
