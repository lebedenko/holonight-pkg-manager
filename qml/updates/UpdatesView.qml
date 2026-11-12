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
    // Optional: the online-check control. Null hides it and leaves the page exactly as it was.
    property var updateCheckModel: null
    // Optional: the transaction view-model. Null hides "Update system…" (system-upgrade REQ-F-078).
    property var transactionController: null

    // The helper's protocol version, asked without starting the helper (system-upgrade DESIGN D-11).
    Component.onCompleted: {
        if (root.transactionController !== null) {
            root.transactionController.probeHelper()
        }
    }

    readonly property bool hasResult: root.updatesModel.state === UpdatesModel.Updates
                                      || root.updatesModel.state === UpdatesModel.UpToDate
    readonly property string reloadBannerText:
        qsTr("Current update evaluation failed: %1. Showing previously loaded data.")
            .arg(root.updatesModel.reloadErrorMessage)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        UpdatesCheckBar {
            updateCheckModel: root.updateCheckModel
            titleText: qsTr("Updates")
            Layout.fillWidth: true

            Controls.Button {
                objectName: "updatesReloadButton"
                text: root.updatesModel.loading ? qsTr("Loading…") : qsTr("Reload")
                enabled: !root.updatesModel.loading
                Controls.ToolTip.text: qsTr("Re-read the package databases on this computer")
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.delay: 500

                onClicked: root.updatesModel.reload()
            }

            // Enabled whatever the list shows: the list comes from the databases on disk, which may be stale, and only
            // the helper's own sync knows (system-upgrade REQ-F-078). Disabled for an old helper (REQ-F-079) and while
            // a transaction runs.
            Controls.Button {
                objectName: "updateSystemButton"
                visible: root.transactionController !== null
                text: qsTr("Update system…")
                enabled: root.transactionController !== null && root.transactionController.upgradeEnabled
                Controls.ToolTip.text: root.transactionController !== null
                                       && root.transactionController.upgradeDisabledReason.length > 0
                                       ? root.transactionController.upgradeDisabledReason
                                       : qsTr("Download the newest package databases and update every package")
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.delay: 500

                onClicked: root.transactionController.requestUpgrade()
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
                titleText: root.updatesModel.emptyStateText
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
