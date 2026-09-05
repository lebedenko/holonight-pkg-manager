pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Controls

ColumnLayout {
    id: root

    spacing: 0

    HnActionDelegate {
        objectName: "packageDetailFilesLink"
        title: qsTr("Files")
        iconSource: "qrc:/qt/qml/Holonight/Controls/assets/folder.svg"
        showChevron: false
        enabled: false
        Layout.fillWidth: true
    }

    HnActionDelegate {
        objectName: "packageDetailDependenciesLink"
        title: qsTr("Dependencies")
        iconSource: "qrc:/qt/qml/Holonight/Controls/assets/wrench.svg"
        showChevron: false
        enabled: false
        Layout.fillWidth: true
    }

    HnActionDelegate {
        objectName: "packageDetailChangelogLink"
        title: qsTr("Changelog")
        iconSource: "qrc:/qt/qml/Holonight/Controls/assets/paperclip.svg"
        showChevron: false
        enabled: false
        Layout.fillWidth: true
    }

    HnActionDelegate {
        objectName: "packageDetailWebsiteLink"
        title: qsTr("Website")
        showChevron: false
        enabled: false
        Layout.fillWidth: true
    }
}
