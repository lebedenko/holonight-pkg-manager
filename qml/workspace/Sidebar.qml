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

    ColumnLayout {
        anchors.fill: parent
        spacing: 24

        HnAppTitle {
            objectName: "sidebarAppTitle"
            applicationName: qsTr("Packages")
            iconSource: "qrc:/HolonightPackages/assets/holonight-pkg-manager.svg"
            Layout.leftMargin: 8
            Layout.topMargin: 8
        }

        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true

            HnNavigationDelegate {
                objectName: "sidebarUpdatesNav"
                title: qsTr("Updates")
                enabled: false
                Layout.fillWidth: true

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
                checked: true
                Layout.fillWidth: true

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
