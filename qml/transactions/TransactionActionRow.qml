pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts

// The right-aligned button row shared by the pages. A button exists only when its id is in controller.actions.
RowLayout {
    id: root

    required property var controller

    spacing: 8

    Item {
        Layout.fillWidth: true
    }

    Controls.Button {
        objectName: "transactionTryAgainButton"
        visible: root.controller.actions.indexOf("tryAgain") >= 0
        text: qsTr("Try again")
        onClicked: root.controller.tryAgain()
    }

    // A PartialUpgradeRequired refusal of an install offers the system upgrade (system-upgrade REQ-F-089).
    Controls.Button {
        objectName: "transactionUpdateSystemButton"
        visible: root.controller.actions.indexOf("updateSystem") >= 0
        enabled: root.controller.upgradeEnabled
        Controls.ToolTip.visible: hovered && root.controller.upgradeDisabledReason.length > 0
        Controls.ToolTip.text: root.controller.upgradeDisabledReason
        text: qsTr("Update system…")
        onClicked: root.controller.updateSystem()
    }

    Controls.Button {
        objectName: "transactionCancelButton"
        visible: root.controller.actions.indexOf("cancel") >= 0
        text: qsTr("Cancel")
        onClicked: root.controller.dismiss()
    }

    Controls.Button {
        objectName: "transactionCloseButton"
        visible: root.controller.actions.indexOf("close") >= 0
        text: qsTr("Close")
        onClicked: root.controller.dismiss()
    }

    Controls.Button {
        objectName: "transactionConfirmButton"
        visible: root.controller.actions.indexOf("confirm") >= 0
        enabled: root.controller.confirmEnabled
        highlighted: true
        text: root.controller.confirmLabel
        onClicked: root.controller.confirm()
    }
}
