pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// Page title row with the online-check controls, and the quiet status line under it. Failures are shown inline in the
// control row: no popup, dialog or notification. Everything is hidden when updateCheckModel is null or reports that
// the backend cannot check online.
ColumnLayout {
    id: root

    property var updateCheckModel: null
    property string titleText: ""
    // Extra controls placed at the end of the title row (the Reload button).
    default property alias trailingControls: trailing.data

    readonly property bool checkAvailable: root.updateCheckModel !== null && root.updateCheckModel.available

    spacing: 2

    RowLayout {
        spacing: 12
        Layout.fillWidth: true

        HnLabel {
            role: HnTypographyRole.Heading
            font.bold: true
            rawText: root.titleText
            color: HoloniightPalette.textPrimary
            Layout.fillWidth: true
        }

        HnLabel {
            objectName: "updatesCheckFailure"
            role: HnTypographyRole.Caption
            rawText: root.checkAvailable ? root.updateCheckModel.failureText : ""
            visible: root.checkAvailable && root.updateCheckModel.failureText.length > 0
            elide: Text.ElideRight
            color: HoloniightPalette.error
            Layout.maximumWidth: 520
            Layout.fillWidth: false
            Accessible.role: Accessible.AlertMessage
            Accessible.name: rawText
        }

        Controls.ProgressBar {
            objectName: "updatesCheckBusy"
            indeterminate: true
            visible: root.checkAvailable && root.updateCheckModel.checking
            Layout.preferredWidth: 64
        }

        Controls.Button {
            objectName: "updatesCheckNowButton"
            text: root.checkAvailable && root.updateCheckModel.checking ? qsTr("Checking…") : qsTr("Check now")
            visible: root.checkAvailable
            enabled: root.checkAvailable && root.updateCheckModel.checkNowEnabled

            onClicked: root.updateCheckModel.checkNow()
        }

        RowLayout {
            id: trailing

            spacing: 12
        }
    }

    HnLabel {
        objectName: "updatesCheckStatusLine"
        role: HnTypographyRole.Caption
        rawText: root.checkAvailable ? root.updateCheckModel.statusLineText : ""
        visible: root.checkAvailable
        color: HoloniightPalette.textMuted
        Layout.fillWidth: true
    }
}
