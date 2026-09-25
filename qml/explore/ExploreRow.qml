pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import "../packages"

// HnListDelegate, not the qmldir-internal HnSelectableDelegate (see PackageTableRow.qml). Contains no action
// buttons: Explore is read-only.
HnListDelegate {
    id: root

    required property string name
    required property string description
    required property string repository
    required property string availableVersion
    required property string downloadSizeLabel
    required property string installedBadgeText
    required property bool installedVersionDiffers
    required property bool isInstalled

    required property ExploreColumns columns

    implicitHeight: 64
    leftPadding: root.columns.padding
    rightPadding: root.columns.padding
    selectionStyle: HnListDelegate.AccentEdge
    Accessible.name: root.isInstalled ? qsTr("%1, %2").arg(root.name).arg(root.installedBadgeText) : root.name
    Accessible.description: root.description

    contentItem: RowLayout {
        spacing: root.columns.spacing

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Layout.minimumWidth: 80

            HnLabel {
                objectName: "exploreRowName"
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
                objectName: "exploreRowDescription"
                role: HnTypographyRole.Caption
                rawText: root.description
                color: HoloniightPalette.textMuted
                elide: Text.ElideRight
                maximumLineCount: 1
                visible: text.length > 0
                Controls.ToolTip.text: text
                Controls.ToolTip.visible: truncated && descriptionHover.hovered
                Controls.ToolTip.delay: 500
                HoverHandler { id: descriptionHover }
                Layout.fillWidth: true
            }
        }

        HnLabel {
            objectName: "exploreRowVersion"
            role: HnTypographyRole.Body
            rawText: root.availableVersion
            color: HoloniightPalette.textSecondary
            elide: Text.ElideMiddle
            Controls.ToolTip.text: text
            Controls.ToolTip.visible: truncated && versionHover.hovered
            Controls.ToolTip.delay: 500
            HoverHandler { id: versionHover }
            Layout.minimumWidth: root.columns.version
            Layout.maximumWidth: root.columns.version
            Layout.preferredWidth: root.columns.version
            Layout.alignment: Qt.AlignVCenter
        }

        Item {
            Layout.minimumWidth: root.columns.repository
            Layout.maximumWidth: root.columns.repository
            Layout.preferredWidth: root.columns.repository
            Layout.preferredHeight: repositoryBadge.implicitHeight
            Layout.alignment: Qt.AlignVCenter

            PackageOriginBadge {
                id: repositoryBadge

                objectName: "exploreRowRepository"
                text: root.repository
                emphasized: true
                maximumWidth: parent.width
                toolTipText: qsTr("Repository: %1").arg(root.repository)
            }
        }

        HnLabel {
            objectName: "exploreRowDownloadSize"
            role: HnTypographyRole.Body
            rawText: root.downloadSizeLabel
            color: HoloniightPalette.textSecondary
            horizontalAlignment: Text.AlignRight
            Layout.minimumWidth: root.columns.size
            Layout.maximumWidth: root.columns.size
            Layout.preferredWidth: root.columns.size
            Layout.alignment: Qt.AlignVCenter
        }

        Item {
            Layout.minimumWidth: root.columns.installed
            Layout.maximumWidth: root.columns.installed
            Layout.preferredWidth: root.columns.installed
            Layout.preferredHeight: installedBadge.implicitHeight
            Layout.alignment: Qt.AlignVCenter

            HnStatusIndicator {
                id: installedBadge

                objectName: "exploreRowInstalledBadge"
                anchors.right: parent.right
                width: Math.min(implicitWidth, parent.width)
                visible: root.isInstalled
                status: root.installedVersionDiffers ? HnStatusIndicator.Neutral : HnStatusIndicator.Success
                text: root.installedBadgeText
            }
        }
    }
}
