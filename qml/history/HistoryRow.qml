pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// One transaction of the History table (REQ-F-026). HnListDelegate, as in PackageTableRow.qml.
// Contains no action buttons: the History page changes no packages (REQ-F-032).
HnListDelegate {
    id: root

    required property string timeLabel
    required property string originLabel
    required property string summary
    required property string statusLabel
    required property string statusKind

    required property HistoryColumns columns

    readonly property string summaryText: root.summary.length > 0 ? root.summary : "—"

    implicitHeight: 44
    leftPadding: root.columns.padding
    rightPadding: root.columns.padding
    selectionStyle: HnListDelegate.AccentEdge
    Accessible.name: qsTr("%1, %2, %3, %4").arg(root.timeLabel).arg(root.originLabel).arg(root.summaryText)
                                            .arg(root.statusLabel)

    contentItem: RowLayout {
        spacing: root.columns.spacing

        HnLabel {
            objectName: "historyRowTime"
            role: HnTypographyRole.Body
            rawText: root.timeLabel
            color: HoloniightPalette.textSecondary
            Layout.minimumWidth: root.columns.time
            Layout.maximumWidth: root.columns.time
            Layout.preferredWidth: root.columns.time
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            objectName: "historyRowOrigin"
            role: HnTypographyRole.Body
            rawText: root.originLabel
            color: HoloniightPalette.textSecondary
            elide: Text.ElideRight
            Controls.ToolTip.text: text
            Controls.ToolTip.visible: truncated && originHover.hovered
            Controls.ToolTip.delay: 500
            HoverHandler { id: originHover }
            Layout.minimumWidth: root.columns.origin
            Layout.maximumWidth: root.columns.origin
            Layout.preferredWidth: root.columns.origin
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            objectName: "historyRowSummary"
            role: HnTypographyRole.Body
            rawText: root.summaryText
            color: HoloniightPalette.textPrimary
            elide: Text.ElideRight
            Controls.ToolTip.text: text
            Controls.ToolTip.visible: truncated && summaryHover.hovered
            Controls.ToolTip.delay: 500
            HoverHandler { id: summaryHover }
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Layout.alignment: Qt.AlignVCenter
        }

        HnLabel {
            objectName: "historyRowStatus"
            role: HnTypographyRole.Body
            rawText: root.statusLabel
            color: root.statusKind === "interrupted" ? HoloniightPalette.warning
                                                     : root.statusKind === "inProgress" ? HoloniightPalette.textPrimary
                                                                                        : HoloniightPalette.textMuted
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            Layout.minimumWidth: root.columns.status
            Layout.maximumWidth: root.columns.status
            Layout.preferredWidth: root.columns.status
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
