pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

HnActionBar {
    id: root

    required property InstalledPackagesModel installedPackagesModel

    leadingContent: Component {
        RowLayout {
            spacing: 8

            HnIcon {
                source: "qrc:/qt/qml/Holonight/Controls/assets/folder.svg"
                size: 16
                iconState: HnIcon.Muted
            }

            HnLabel {
                objectName: "orphanFooterSummaryLabel"
                role: HnTypographyRole.Body
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                rawText: qsTr("%1 orphaned packages · %2 can be reclaimed")
                    .arg(root.installedPackagesModel.orphanPackageCount)
                    .arg(root.installedPackagesModel.formatSize(root.installedPackagesModel.reclaimableSizeBytes))
                color: HoloniightPalette.textSecondary
            }
        }
    }

    trailingContent: Component {
        Controls.Button {
            objectName: "orphanFooterReviewButton"
            text: qsTr("Review")

            Controls.ToolTip.text: qsTr("Not implemented yet")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 500
        }
    }
}
