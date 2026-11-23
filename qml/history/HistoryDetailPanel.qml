pragma ComponentBehavior: Bound

import QtQuick
import HolonightPackages
import "../packages"

// Details of the selected transaction (REQ-F-030), in the shared detail shell: its empty state while nothing is
// selected and a Loader-instantiated content otherwise.
PackageDetailFrame {
    id: root

    required property HistoryFilterModel filterModel

    // A fully-keyed fallback so bindings never see `undefined` while the Loader deactivates.
    readonly property var emptyEntry: ({
        sequence: 0, dayKey: "", timeLabel: "", originKind: "unknown", originLabel: "", commandLine: "", summary: "",
        statusKind: "completed", statusLabel: "", actions: [], preHooks: [], hooks: [], messages: [],
        packageNames: [],
    })
    readonly property var currentEntry: root.hasSelection ? root.filterModel.currentEntry : root.emptyEntry

    hasSelection: root.filterModel.currentRow >= 0
    emptyTitle: qsTr("Select a transaction to view details")
    detailContent: detailContentComponent

    Component {
        id: detailContentComponent

        HistoryDetailContent {
            entry: root.currentEntry
        }
    }
}
