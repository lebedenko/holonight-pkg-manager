pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
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

    required property PackageTableColumns columns
    readonly property string reasonLabel: root.installReason === "explicit" ? qsTr("Explicit") : qsTr("Dependency")
    readonly property bool isOfficial: root.sourceLabel === "official"
    readonly property string originText: root.isOfficial
        ? (root.repository.length > 0 ? root.repository : qsTr("Official"))
        : qsTr("Foreign")

    implicitHeight: 64
    leftPadding: root.columns.padding
    rightPadding: root.columns.padding
    selectionStyle: HnListDelegate.AccentEdge
    Accessible.name: root.name
    Accessible.description: root.description

    contentItem: RowLayout {
        spacing: root.columns.spacing

        Controls.CheckBox {
            objectName: "packageRowCheckBox"
            Layout.minimumWidth: root.columns.checkbox
            Layout.maximumWidth: root.columns.checkbox
            Layout.preferredWidth: root.columns.checkbox
            Layout.alignment: Qt.AlignVCenter

            Controls.ToolTip.text: qsTr("Not implemented yet")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 500
        }

        Rectangle {
            Layout.preferredWidth: root.columns.icon
            Layout.preferredHeight: root.columns.icon
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
                Controls.ToolTip.text: text
                Controls.ToolTip.visible: truncated && nameHover.hovered
                Controls.ToolTip.delay: 500
                HoverHandler { id: nameHover }
                Layout.fillWidth: true
            }

            HnLabel {
                role: HnTypographyRole.Caption
                rawText: root.description
                Controls.ToolTip.text: text
                Controls.ToolTip.visible: truncated && descriptionHover.hovered
                Controls.ToolTip.delay: 500
                HoverHandler { id: descriptionHover }
                color: HoloniightPalette.textMuted
                elide: Text.ElideRight
                maximumLineCount: 1
                visible: text.length > 0
                Layout.fillWidth: true
            }
        }

        Item {
            Layout.minimumWidth: root.columns.origin
            Layout.maximumWidth: root.columns.origin
            Layout.preferredWidth: root.columns.origin
            Layout.preferredHeight: originBadge.implicitHeight
            Layout.alignment: Qt.AlignVCenter

            PackageOriginBadge {
                id: originBadge
                objectName: "packageRowOriginBadge"

                text: root.originText
                emphasized: root.isOfficial
                maximumWidth: parent.width
                toolTipText: root.isOfficial && root.repository.length > 0
                    ? qsTr("Repository: %1").arg(root.repository) : text
            }
        }

        HnLabel {
            role: HnTypographyRole.Body
            rawText: root.installedVersion
            Controls.ToolTip.text: text
            Controls.ToolTip.visible: truncated && versionHover.hovered
            Controls.ToolTip.delay: 500
            HoverHandler { id: versionHover }
            color: HoloniightPalette.textSecondary
            elide: Text.ElideRight
            Layout.minimumWidth: root.columns.version
            Layout.maximumWidth: root.columns.version
            Layout.preferredWidth: root.columns.version
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            role: HnTypographyRole.Body
            elide: Text.ElideRight
            rawText: root.sizeLabel
            Controls.ToolTip.text: text
            Controls.ToolTip.visible: truncated && sizeHover.hovered
            Controls.ToolTip.delay: 500
            HoverHandler { id: sizeHover }
            color: HoloniightPalette.textSecondary
            Layout.minimumWidth: root.columns.size
            Layout.maximumWidth: root.columns.size
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: root.columns.size
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            role: HnTypographyRole.Body
            elide: Text.ElideRight
            rawText: root.reasonLabel
            Controls.ToolTip.text: text
            Controls.ToolTip.visible: truncated && reasonHover.hovered
            Controls.ToolTip.delay: 500
            HoverHandler { id: reasonHover }
            color: HoloniightPalette.textSecondary
            Layout.minimumWidth: root.columns.reason
            Layout.maximumWidth: root.columns.reason
            Layout.preferredWidth: root.columns.reason
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
