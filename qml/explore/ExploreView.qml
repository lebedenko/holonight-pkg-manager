pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import HolonightPackages
import "../updates"

// Read-only search over configured sync repositories. Root is a plain Item; all state lives in the ExploreModel, so
// destroying and recreating this page loses nothing.
Item {
    id: root

    required property ExploreModel exploreModel

    readonly property bool hasIndex: root.exploreModel.state === ExploreModel.Hint
                                     || root.exploreModel.state === ExploreModel.NoMatches
                                     || root.exploreModel.state === ExploreModel.Results
    readonly property string reloadBannerText: root.exploreModel.dataAsOf.getTime() > 0
        ? qsTr("Reload failed: %1. Showing data from %2.")
            .arg(root.exploreModel.reloadErrorMessage)
            .arg(root.exploreModel.dataAsOf.toLocaleString(Qt.locale(), Locale.ShortFormat))
        : qsTr("Reload failed: %1.").arg(root.exploreModel.reloadErrorMessage)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        ExploreHeadline {
            objectName: "exploreHeadline"
            exploreModel: root.exploreModel
            Layout.fillWidth: true
        }

        UpdatesInlineNotice {
            objectName: "exploreReloadBanner"
            visible: root.exploreModel.reloadErrorMessage.length > 0
            status: HnStatusIndicator.Error
            text: root.reloadBannerText
            Layout.fillWidth: true
        }

        UpdatesInlineNotice {
            objectName: "exploreStaleHint"
            visible: root.hasIndex && root.exploreModel.databasesStale
            status: HnStatusIndicator.Warning
            text: root.exploreModel.staleHintText
            Layout.fillWidth: true
        }

        Controls.ProgressBar {
            objectName: "exploreProgressBar"
            indeterminate: true
            visible: root.exploreModel.loading
            Layout.fillWidth: true
        }

        HnSearchField {
            objectName: "exploreSearchField"
            enabled: root.exploreModel.searchEnabled
            placeholderText: root.exploreModel.searchEnabled ? qsTr("Search configured repositories")
                                                             : qsTr("Loading package index…")
            text: root.exploreModel.searchText
            Layout.fillWidth: true

            onTextChanged: root.exploreModel.searchText = text
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridLayout {
                id: resultsLayout

                anchors.fill: parent
                visible: root.exploreModel.state === ExploreModel.Results
                columns: width >= 1092 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                ExploreTable {
                    exploreModel: root.exploreModel
                    Layout.minimumHeight: 160
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                ExploreDetailPanel {
                    objectName: "exploreDetailPanel"
                    exploreModel: root.exploreModel
                    Layout.minimumHeight: 200
                    Layout.preferredWidth: 300
                    Layout.fillWidth: resultsLayout.columns === 1
                    Layout.fillHeight: true
                }
            }

            HnLoadingState {
                objectName: "loadingState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 360))
                visible: root.exploreModel.state === ExploreModel.Loading
                titleText: qsTr("Loading package index…")
            }

            HnEmptyState {
                objectName: "noDatabasesState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.exploreModel.state === ExploreModel.NoDatabases
                titleText: qsTr("No package databases found.")
                descriptionText: qsTr("Arch package management requires at least one repository to be configured in pacman.conf.")
            }

            HnEmptyState {
                objectName: "errorState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.exploreModel.state === ExploreModel.Error
                titleText: qsTr("Couldn't load packages")
                descriptionText: root.exploreModel.errorMessage
            }

            HnEmptyState {
                objectName: "hintState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.exploreModel.state === ExploreModel.Hint
                titleText: qsTr("Enter a search term to find packages")
                descriptionText: root.exploreModel.repositoryScopeNote
            }

            HnEmptyState {
                objectName: "noMatchesState"
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 48, 420))
                visible: root.exploreModel.state === ExploreModel.NoMatches
                titleText: root.exploreModel.noMatchesText
            }
        }
    }
}
