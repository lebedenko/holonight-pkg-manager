pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

RowLayout {
    id: root

    required property ExploreModel exploreModel

    spacing: 12

    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true

        HnLabel {
            role: HnTypographyRole.Heading
            font.bold: true
            rawText: qsTr("Explore")
            color: HoloniightPalette.textPrimary
        }

        HnLabel {
            objectName: "exploreHeadlineDataAsOf"
            role: HnTypographyRole.Caption
            rawText: root.exploreModel.dataAsOfLabel
            visible: text.length > 0
            color: HoloniightPalette.textMuted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        HnLabel {
            objectName: "exploreHeadlineRepositoryScope"
            role: HnTypographyRole.Caption
            rawText: root.exploreModel.repositoryScopeNote
            color: HoloniightPalette.textMuted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    Controls.Button {
        objectName: "exploreReloadButton"
        text: root.exploreModel.loading ? qsTr("Loading…") : qsTr("Reload")
        enabled: !root.exploreModel.loading
        Layout.alignment: Qt.AlignTop
        Controls.ToolTip.text: qsTr("Re-read the package databases on this computer")
        Controls.ToolTip.visible: hovered
        Controls.ToolTip.delay: 500

        onClicked: root.exploreModel.reload()
    }
}
