pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

HnSurfaceFrame {
    id: root

    required property UpdatesModel updatesModel

    readonly property int updateCount: root.updatesModel.updateCount
    readonly property string countText: root.updateCount === 1
        ? qsTr("1 update") : qsTr("%1 updates").arg(root.updateCount)

    implicitHeight: content.implicitHeight + 32

    GridLayout {
        id: content

        anchors.fill: parent
        anchors.margins: 16
        columns: root.width < 560 ? 1 : 3
        columnSpacing: 32
        rowSpacing: 12

        ColumnLayout {
            spacing: 2

            HnLabel {
                role: HnTypographyRole.Caption
                rawText: qsTr("Pending updates")
                color: HoloniightPalette.textMuted
            }

            HnLabel {
                objectName: "updatesHeadlineCount"
                role: HnTypographyRole.Heading
                font.bold: true
                rawText: root.countText
                color: HoloniightPalette.textPrimary
            }

            HnLabel {
                objectName: "updatesHeadlineIgnored"
                role: HnTypographyRole.Caption
                visible: root.updatesModel.ignoredCount > 0
                rawText: qsTr("%1 ignored, not counted").arg(root.updatesModel.ignoredCount)
                color: HoloniightPalette.textMuted
            }
        }

        ColumnLayout {
            spacing: 2

            HnLabel {
                role: HnTypographyRole.Caption
                rawText: qsTr("Total download")
                color: HoloniightPalette.textMuted
            }

            HnLabel {
                objectName: "updatesHeadlineDownload"
                role: HnTypographyRole.Heading
                font.bold: true
                rawText: root.updatesModel.totalDownloadLabel
                color: HoloniightPalette.textPrimary
            }
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true

            HnLabel {
                objectName: "updatesHeadlineDataAsOf"
                role: HnTypographyRole.Body
                rawText: root.updatesModel.dataAsOfLabel
                color: HoloniightPalette.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            HnLabel {
                objectName: "updatesHeadlineOfficialOnly"
                role: HnTypographyRole.Caption
                rawText: root.updatesModel.officialOnlyNote
                color: HoloniightPalette.textMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
}
