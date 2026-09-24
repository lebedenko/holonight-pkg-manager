pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// HnListDelegate, not the qmldir-internal HnSelectableDelegate (see PackageTableRow.qml).
HnListDelegate {
    id: root

    required property string name
    required property string installedVersion
    required property string availableVersion
    required property string repository
    required property string downloadSizeLabel
    required property string sizeDeltaLabel
    required property bool isIgnored

    required property UpdatesColumns columns

    readonly property color primaryTextColor: root.isIgnored ? HoloniightPalette.textMuted : HoloniightPalette.textPrimary
    readonly property color secondaryTextColor: root.isIgnored ? HoloniightPalette.textMuted : HoloniightPalette.textSecondary

    implicitHeight: 48
    leftPadding: root.columns.padding
    rightPadding: root.columns.padding
    Accessible.name: root.isIgnored ? qsTr("%1, ignored").arg(root.name) : root.name

    contentItem: RowLayout {
        spacing: root.columns.spacing

        HnLabel {
            objectName: "updateRowName"
            role: HnTypographyRole.Body
            rawText: root.name
            color: root.primaryTextColor
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 80
        }

        HnLabel {
            objectName: "updateRowVersions"
            role: HnTypographyRole.Body
            rawText: qsTr("%1 → %2").arg(root.installedVersion).arg(root.availableVersion)
            Controls.ToolTip.text: text
            Controls.ToolTip.visible: truncated && versionsHover.hovered
            Controls.ToolTip.delay: 500
            HoverHandler { id: versionsHover }
            color: root.secondaryTextColor
            elide: Text.ElideMiddle
            Layout.minimumWidth: root.columns.versions
            Layout.maximumWidth: root.columns.versions
            Layout.preferredWidth: root.columns.versions
        }

        HnLabel {
            objectName: "updateRowRepository"
            role: HnTypographyRole.Body
            rawText: root.repository
            color: root.secondaryTextColor
            elide: Text.ElideRight
            Layout.minimumWidth: root.columns.repository
            Layout.maximumWidth: root.columns.repository
            Layout.preferredWidth: root.columns.repository
        }

        HnLabel {
            objectName: "updateRowDownloadSize"
            role: HnTypographyRole.Body
            rawText: root.downloadSizeLabel
            color: root.secondaryTextColor
            horizontalAlignment: Text.AlignRight
            Layout.minimumWidth: root.columns.download
            Layout.maximumWidth: root.columns.download
            Layout.preferredWidth: root.columns.download
        }

        HnLabel {
            objectName: "updateRowSizeDelta"
            role: HnTypographyRole.Body
            rawText: root.sizeDeltaLabel
            color: root.secondaryTextColor
            horizontalAlignment: Text.AlignRight
            Layout.minimumWidth: root.columns.delta
            Layout.maximumWidth: root.columns.delta
            Layout.preferredWidth: root.columns.delta
        }

        Item {
            Layout.minimumWidth: root.columns.ignored
            Layout.maximumWidth: root.columns.ignored
            Layout.preferredWidth: root.columns.ignored
            Layout.preferredHeight: ignoredBadge.implicitHeight

            HnStatusIndicator {
                id: ignoredBadge

                objectName: "updateRowIgnoredBadge"
                anchors.right: parent.right
                visible: root.isIgnored
                status: HnStatusIndicator.Warning
                text: qsTr("Ignored")
                Controls.ToolTip.text: qsTr("Listed in IgnorePkg or IgnoreGroup in pacman.conf")
                Controls.ToolTip.visible: ignoredHover.hovered
                Controls.ToolTip.delay: 500
                HoverHandler { id: ignoredHover }
            }
        }
    }
}
