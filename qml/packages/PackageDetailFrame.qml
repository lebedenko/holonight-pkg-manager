pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Holonight.Core
import Holonight.Controls

// Shell shared by the package detail panels: surface, empty state and a scrolling area that instantiates
// `detailContent` only while a package is selected.
HnSurfaceFrame {
    id: root

    required property bool hasSelection
    property Component detailContent
    property string emptyTitle: qsTr("Select a package to view details")

    surfaceRole: HnSurfaceRole.Panel

    HnEmptyState {
        objectName: "packageDetailEmptyState"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 280)
        visible: !root.hasSelection
        titleText: root.emptyTitle
    }

    Controls.ScrollView {
        id: detailScroll
        objectName: "packageDetailScrollView"
        anchors.fill: parent
        visible: root.hasSelection
        contentWidth: availableWidth
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
        Controls.ScrollBar.vertical: Controls.ScrollBar {}
        clip: true

        Loader {
            width: detailScroll.availableWidth
            active: root.hasSelection
            sourceComponent: root.detailContent
        }
    }
}
