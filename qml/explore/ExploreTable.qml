pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

// Header and rows scroll horizontally together on narrow windows, as in PackageTable.qml.
Controls.ScrollView {
    id: root

    required property ExploreModel exploreModel

    ExploreColumns { id: columnSizes }

    objectName: "exploreTable"
    implicitWidth: 0
    implicitHeight: 0
    contentWidth: Math.max(availableWidth, columnSizes.minimumWidth)
    contentHeight: availableHeight
    clip: true
    Controls.ScrollBar.horizontal: Controls.ScrollBar {}
    Controls.ScrollBar.vertical.policy: Controls.ScrollBar.AlwaysOff

    ColumnLayout {
        width: root.contentWidth
        height: root.availableHeight
        spacing: 0

        Item {
            implicitHeight: header.implicitHeight + 16
            Layout.fillWidth: true

            RowLayout {
                id: header

                anchors.fill: parent
                anchors.leftMargin: columnSizes.padding
                anchors.rightMargin: columnSizes.padding
                spacing: columnSizes.spacing

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Package")
                    color: HoloniightPalette.textMuted
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                }

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Version")
                    color: HoloniightPalette.textMuted
                    Layout.minimumWidth: columnSizes.version
                    Layout.maximumWidth: columnSizes.version
                    Layout.preferredWidth: columnSizes.version
                }

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Repository")
                    color: HoloniightPalette.textMuted
                    Layout.minimumWidth: columnSizes.repository
                    Layout.maximumWidth: columnSizes.repository
                    Layout.preferredWidth: columnSizes.repository
                }

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Download")
                    color: HoloniightPalette.textMuted
                    horizontalAlignment: Text.AlignRight
                    Layout.minimumWidth: columnSizes.size
                    Layout.maximumWidth: columnSizes.size
                    Layout.preferredWidth: columnSizes.size
                }

                Item {
                    Layout.preferredWidth: columnSizes.installed
                }
            }

            HnSeparator {
                crossAxisAlignment: HnSeparator.Trailing
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
            }
        }

        ListView {
            id: resultList

            objectName: "exploreList"
            clip: true
            model: root.exploreModel
            currentIndex: root.exploreModel.currentRow
            keyNavigationEnabled: false
            Layout.fillWidth: true
            Layout.fillHeight: true
            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            // Keep keyboard selection in the same model as clicks and result reconciliation.
            Keys.onDownPressed: {
                if (resultList.count > 0)
                    root.exploreModel.currentRow = Math.min(resultList.count - 1, root.exploreModel.currentRow + 1)
            }
            Keys.onUpPressed: {
                if (resultList.count > 0)
                    root.exploreModel.currentRow = Math.max(0, root.exploreModel.currentRow - 1)
            }

            delegate: ExploreRow {
                id: delegate

                columns: columnSizes

                required property int index

                width: ListView.view.width
                highlighted: ListView.isCurrentItem

                onClicked: root.exploreModel.currentRow = delegate.index
            }
        }

        HnLabel {
            objectName: "exploreCapFooter"
            role: HnTypographyRole.Caption
            rawText: root.exploreModel.footerText
            color: HoloniightPalette.textMuted
            wrapMode: Text.Wrap
            visible: root.exploreModel.footerText.length > 0
            Layout.fillWidth: true
            Layout.margins: columnSizes.padding
        }
    }
}
