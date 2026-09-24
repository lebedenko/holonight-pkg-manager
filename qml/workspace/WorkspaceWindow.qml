import QtQuick
import QtQuick.Layouts
import Holonight.Core
import HolonightPackages
import "../packages"
import "../updates"

Rectangle {
    id: root

    required property InstalledPackagesModel installedPackagesModel
    required property UpdatesModel updatesModel

    // Installed stays the landing page until the Updates page gains an advisor and update actions.
    property string currentPage: "installed"

    width: 1360
    height: 890
    color: HoloniightPalette.background

    Component.onCompleted: HoloniightPalette.reload()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            currentPage: root.currentPage
            Layout.preferredWidth: 196
            Layout.fillHeight: true
            Layout.margins: 12

            onPageRequested: page => root.currentPage = page
        }

        StackLayout {
            objectName: "workspacePages"
            currentIndex: root.currentPage === "updates" ? 1 : 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            InstalledPackagesView {
                objectName: "installedPage"
                installedPackagesModel: root.installedPackagesModel
            }

            UpdatesView {
                objectName: "updatesPage"
                updatesModel: root.updatesModel
            }
        }
    }
}
