pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import "../packages"

// One transaction: header, the command line for pacman runs only, the per-package actions, the hooks before and
// after the transaction, and the warnings and errors (REQ-F-030). The model carries no scriptlet text (REQ-F-011).
// Contains no button: the History page changes no packages (REQ-F-032).
Item {
    id: root

    // A HistoryFilterModel.currentEntry map.
    required property var entry

    readonly property string startedText: root.entry.startedAt
        ? qsTr("%1 at %2").arg(root.entry.startedAt.toLocaleDateString(Qt.locale(), Locale.LongFormat))
                           .arg(root.entry.timeLabel)
        : root.entry.timeLabel

    implicitHeight: content.implicitHeight + 32

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 16

        HnLabel {
            objectName: "historyDetailTitle"
            role: HnTypographyRole.Title
            rawText: root.entry.summary.length > 0 ? root.entry.summary : qsTr("No package changes")
            color: HoloniightPalette.textPrimary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        PackageDetailMetadataRows {
            rows: [
                { label: qsTr("Started"), value: root.startedText },
                { label: qsTr("Origin"), value: root.entry.originLabel },
                { label: qsTr("Status"), value: root.entry.statusLabel }
            ]
            Layout.fillWidth: true
        }

        // Structurally absent unless the run came from pacman itself.
        Loader {
            active: root.entry.originKind === "pacman" && root.entry.commandLine.length > 0
            visible: active
            Layout.fillWidth: true

            sourceComponent: ColumnLayout {
                spacing: 4

                HnSectionHeader {
                    titleText: qsTr("Command")
                    dividerVisible: false
                    Layout.fillWidth: true
                }

                HnLabel {
                    objectName: "historyDetailCommand"
                    role: HnTypographyRole.Body
                    font.family: "monospace"
                    rawText: root.entry.commandLine
                    color: HoloniightPalette.textSecondary
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }

        HistoryDetailSection {
            objectName: "historyDetailActions"
            titleText: qsTr("Packages")
            count: root.entry.actions.length
            Layout.fillWidth: true

            Repeater {
                model: root.entry.actions

                RowLayout {
                    id: actionRow

                    required property var modelData

                    spacing: 8
                    Layout.fillWidth: true

                    HnLabel {
                        objectName: "historyDetailActionName"
                        role: HnTypographyRole.Body
                        rawText: actionRow.modelData.name
                        color: HoloniightPalette.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 60
                    }

                    HnLabel {
                        objectName: "historyDetailActionKind"
                        role: HnTypographyRole.Caption
                        rawText: actionRow.modelData.kindLabel
                        color: HoloniightPalette.textMuted
                    }

                    HnLabel {
                        objectName: "historyDetailActionVersion"
                        role: HnTypographyRole.Body
                        rawText: actionRow.modelData.versionText
                        color: HoloniightPalette.textSecondary
                        horizontalAlignment: Text.AlignRight
                        wrapMode: Text.WrapAnywhere
                        Layout.maximumWidth: content.width / 2
                    }
                }
            }
        }

        HistoryDetailSection {
            objectName: "historyDetailPreHooks"
            titleText: qsTr("Hooks before")
            count: root.entry.preHooks.length
            Layout.fillWidth: true

            Repeater {
                model: root.entry.preHooks

                HnLabel {
                    required property string modelData

                    role: HnTypographyRole.Body
                    rawText: modelData
                    color: HoloniightPalette.textSecondary
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }

        HistoryDetailSection {
            objectName: "historyDetailPostHooks"
            titleText: qsTr("Hooks after")
            count: root.entry.hooks.length
            Layout.fillWidth: true

            Repeater {
                model: root.entry.hooks

                HnLabel {
                    required property string modelData

                    role: HnTypographyRole.Body
                    rawText: modelData
                    color: HoloniightPalette.textSecondary
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }

        HistoryDetailSection {
            objectName: "historyDetailMessages"
            titleText: qsTr("Warnings and errors")
            count: root.entry.messages.length
            Layout.fillWidth: true

            Repeater {
                model: root.entry.messages

                HnLabel {
                    required property var modelData

                    objectName: "historyDetailMessage"
                    role: HnTypographyRole.Body
                    rawText: modelData.text
                    color: modelData.severity === "error" ? HoloniightPalette.error : HoloniightPalette.warning
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
