pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// HnListDelegate rather than the internal HnSelectableDelegate it wraps: HnSelectableDelegate is
// marked `internal` in Holonight.Controls' qmldir and unresolvable from outside that module at
// QML-engine load time (qmllint/qmlcachegen don't catch this; only actual instantiation does).
HnListDelegate {
    id: root

    required property string name
    required property string description
    required property string sourceLabel
    required property string repository
    required property string installedVersion
    required property string sizeLabel
    required property string installReason

    readonly property int checkboxColumnWidth: 40
    readonly property int originColumnWidth: 160
    readonly property int versionColumnWidth: 120
    readonly property int sizeColumnWidth: 90
    readonly property int reasonColumnWidth: 100
    readonly property string reasonLabel: root.installReason === "explicit" ? qsTr("Explicit") : qsTr("Dependency")
    readonly property bool isOfficial: root.sourceLabel === "official"
    readonly property string originText: root.isOfficial
        ? (root.repository.length > 0 ? qsTr("Official · %1").arg(root.repository) : qsTr("Official"))
        : qsTr("AUR")

    implicitHeight: 64
    selectionStyle: HnListDelegate.AccentEdge
    Accessible.name: root.name
    Accessible.description: root.description

    contentItem: RowLayout {
        spacing: 12

        CheckBox {
            objectName: "packageRowCheckBox"
            Layout.preferredWidth: root.checkboxColumnWidth
            Layout.alignment: Qt.AlignVCenter

            ToolTip.text: qsTr("Not implemented yet")
            ToolTip.visible: hovered
            ToolTip.delay: 500
        }

        Rectangle {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            radius: 8
            color: HoloniightPalette.surfaceElevated

            HnLabel {
                anchors.centerIn: parent
                role: HnTypographyRole.Body
                rawText: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
                color: HoloniightPalette.textSecondary
            }
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true

            HnLabel {
                role: HnTypographyRole.Body
                rawText: root.name
                color: HoloniightPalette.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            HnLabel {
                role: HnTypographyRole.Caption
                rawText: root.description
                color: HoloniightPalette.textMuted
                elide: Text.ElideRight
                maximumLineCount: 1
                visible: text.length > 0
                Layout.fillWidth: true
            }
        }

        PackageOriginBadge {
            text: root.originText
            emphasized: root.isOfficial
            Layout.preferredWidth: root.originColumnWidth
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            role: HnTypographyRole.Body
            rawText: root.installedVersion
            color: HoloniightPalette.textSecondary
            elide: Text.ElideRight
            Layout.preferredWidth: root.versionColumnWidth
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            role: HnTypographyRole.Body
            rawText: root.sizeLabel
            color: HoloniightPalette.textSecondary
            Layout.preferredWidth: root.sizeColumnWidth
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            role: HnTypographyRole.Body
            rawText: root.reasonLabel
            color: HoloniightPalette.textSecondary
            Layout.preferredWidth: root.reasonColumnWidth
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
