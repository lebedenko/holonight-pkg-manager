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
