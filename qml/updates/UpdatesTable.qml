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

    required property UpdatesModel updatesModel

    UpdatesColumns { id: columnSizes }

    objectName: "updatesTable"
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
                    Layout.minimumWidth: columnSizes.versions
                    Layout.maximumWidth: columnSizes.versions
                    Layout.preferredWidth: columnSizes.versions
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
                    Layout.minimumWidth: columnSizes.download
                    Layout.maximumWidth: columnSizes.download
                    Layout.preferredWidth: columnSizes.download
                }

                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: qsTr("Size change")
                    color: HoloniightPalette.textMuted
                    horizontalAlignment: Text.AlignRight
                    Layout.minimumWidth: columnSizes.delta
                    Layout.maximumWidth: columnSizes.delta
                    Layout.preferredWidth: columnSizes.delta
                }

                Item {
                    Layout.preferredWidth: columnSizes.ignored
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
            objectName: "updatesList"
            clip: true
            model: root.updatesModel
            Layout.fillWidth: true
            Layout.fillHeight: true
            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            delegate: UpdateRow {
                columns: columnSizes
                width: ListView.view.width
            }
        }
    }
}
