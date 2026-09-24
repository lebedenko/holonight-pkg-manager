pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// Root is a plain Item (not a bare ColumnLayout) so that Layout.preferredWidth set on the
// instantiating side (WorkspaceWindow.qml) is honored. A Layout type used directly as a
// component's root, when placed as a child of another Layout, sizes itself from its own
// computed implicit width instead of the assigned Layout.preferredWidth -- it starves sibling
// fillWidth items instead of respecting the requested width.
Item {
    id: root

    property string currentPage: "installed"

    signal pageRequested(string page)

    ColumnLayout {
        anchors.fill: parent
        spacing: 24

        ColumnLayout {
            spacing: 2
            Layout.leftMargin: 8
            Layout.topMargin: 8
            Layout.fillWidth: true

            HnAppTitle {
                objectName: "sidebarAppTitle"
                applicationName: ""
                iconSource: "qrc:/HolonightPackages/assets/holonight-pkg-manager.svg"
                Layout.fillWidth: true
            }

            HnLabel {
                role: HnTypographyRole.Title
                rawText: qsTr("Packages")
                color: HoloniightPalette.textPrimary
            }
        }

        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true

            HnNavigationDelegate {
                objectName: "sidebarUpdatesNav"
                title: qsTr("Updates")
                checked: root.currentPage === "updates"
                Layout.fillWidth: true

                onClicked: root.pageRequested("updates")

                leadingContent: HnLabel {
                    role: HnTypographyRole.Body
                    rawText: "⬇"
                }
            }

            HnNavigationDelegate {
                objectName: "sidebarExploreNav"
                title: qsTr("Explore")
                enabled: false
                Layout.fillWidth: true

                leadingContent: HnLabel {
                    role: HnTypographyRole.Body
                    rawText: "◎"
                }
            }

            HnNavigationDelegate {
                objectName: "sidebarInstalledNav"
                title: qsTr("Installed")
                checked: root.currentPage === "installed"
                Layout.fillWidth: true

                onClicked: root.pageRequested("installed")

                leadingContent: HnLabel {
                    role: HnTypographyRole.Body
                    rawText: "▤"
                }
            }

            HnNavigationDelegate {
                objectName: "sidebarHistoryNav"
                title: qsTr("History")
                enabled: false
                Layout.fillWidth: true

                leadingContent: HnLabel {
                    role: HnTypographyRole.Body
                    rawText: "↺"
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        HnLabel {
            objectName: "sidebarLastSyncedLabel"
            role: HnTypographyRole.Caption
            rawText: qsTr("Last synced —")
            color: HoloniightPalette.textMuted
            Layout.leftMargin: 8
        }

        HnNavigationDelegate {
            objectName: "sidebarSettingsNav"
            title: qsTr("Settings")
            enabled: false
            Layout.fillWidth: true

            leadingContent: HnLabel {
                role: HnTypographyRole.Body
                rawText: "⚙"
            }
        }
    }
}
