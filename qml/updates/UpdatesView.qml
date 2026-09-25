pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages

Item {
    id: root

    required property UpdatesModel updatesModel

    readonly property bool hasResult: root.updatesModel.state === UpdatesModel.Updates
                                      || root.updatesModel.state === UpdatesModel.UpToDate
    readonly property string reloadBannerText: root.updatesModel.dataAsOf.getTime() > 0
        ? qsTr("Reload failed: %1. Showing data from %2.")
            .arg(root.updatesModel.reloadErrorMessage)
            .arg(root.updatesModel.dataAsOf.toLocaleString(Qt.locale(), Locale.ShortFormat))
        : qsTr("Reload failed: %1.").arg(root.updatesModel.reloadErrorMessage)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            HnLabel {
                role: HnTypographyRole.Heading
                font.bold: true
                rawText: qsTr("Updates")
                color: HoloniightPalette.textPrimary
                Layout.fillWidth: true
            }

            Controls.Button {
                objectName: "updatesReloadButton"
                text: root.updatesModel.loading ? qsTr("Loading…") : qsTr("Reload")
                enabled: !root.updatesModel.loading
                Controls.ToolTip.text: qsTr("Re-read the package databases on this computer")
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.delay: 500

                onClicked: root.updatesModel.reload()
            }
        }

        UpdatesInlineNotice {
            objectName: "updatesReloadBanner"
            visible: root.updatesModel.reloadErrorMessage.length > 0
            status: HnStatusIndicator.Error
            text: root.reloadBannerText
            Layout.fillWidth: true
        }

        UpdatesHeadline {
            objectName: "updatesHeadline"
            updatesModel: root.updatesModel
            visible: root.hasResult
            Layout.fillWidth: true
        }

        Controls.ProgressBar {
            objectName: "updatesProgressBar"
            indeterminate: true
            visible: root.updatesModel.loading
            Layout.fillWidth: true
        }

        UpdatesInlineNotice {
            objectName: "updatesStaleHint"
            visible: root.hasResult && root.updatesModel.databasesStale
            status: HnStatusIndicator.Warning
            text: root.updatesModel.staleHintText
            Layout.fillWidth: true
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            UpdatesTable {
                anchors.fill: parent
                updatesModel: root.updatesModel
                visible: root.updatesModel.state === UpdatesModel.Updates
            }

            HnLoadingState {
                objectName: "loadingState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 360))
                visible: root.updatesModel.state === UpdatesModel.Loading
                titleText: qsTr("Checking for updates…")
            }

            HnEmptyState {
                objectName: "noDatabasesState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.updatesModel.state === UpdatesModel.NoDatabases
                titleText: qsTr("No package databases found.")
                descriptionText: qsTr("Arch package management requires at least one repository to be configured in pacman.conf.")
            }

            HnEmptyState {
                objectName: "upToDateState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.updatesModel.state === UpdatesModel.UpToDate
                titleText: qsTr("All official-repository packages are up to date.")
                descriptionText: root.updatesModel.officialOnlyNote
            }

            HnEmptyState {
                objectName: "errorState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.updatesModel.state === UpdatesModel.Error
                titleText: qsTr("Couldn't check for updates")
                descriptionText: root.updatesModel.errorMessage
            }
        }
    }
}
