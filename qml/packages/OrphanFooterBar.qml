pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
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
                rawText: qsTr("%1 orphaned packages · %2 can be reclaimed")
                    .arg(root.installedPackagesModel.orphanPackageCount)
                    .arg(root.installedPackagesModel.formatSize(root.installedPackagesModel.reclaimableSizeBytes))
                color: HoloniightPalette.textSecondary
            }
        }
    }

    trailingContent: Component {
        Button {
            objectName: "orphanFooterReviewButton"
            text: qsTr("Review")

            ToolTip.text: qsTr("Not implemented yet")
            ToolTip.visible: hovered
            ToolTip.delay: 500
        }
    }
}
