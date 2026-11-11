pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core

// Only the phase, the current item, a bar and the Cancel control (REQ-F-011): no log, no list, no text area.
ColumnLayout {
    id: root

    required property var controller

    spacing: 12

    HnLabel {
        objectName: "transactionTitle"
        role: HnTypographyRole.Title
        rawText: root.controller.title
        wrapMode: Text.WrapAnywhere
        color: HoloniightPalette.textPrimary
        Layout.fillWidth: true
    }

    HnLabel {
        objectName: "transactionPhase"
        role: HnTypographyRole.Body
        rawText: root.controller.phaseLabel
        color: HoloniightPalette.textPrimary
        Layout.fillWidth: true
    }

    HnLabel {
        objectName: "transactionItem"
        role: HnTypographyRole.Caption
        rawText: root.controller.currentItem
        elide: Text.ElideMiddle
        color: HoloniightPalette.textSecondary
        Layout.fillWidth: true
    }

    Controls.ProgressBar {
        objectName: "transactionProgressBar"
        indeterminate: root.controller.progressIndeterminate
        value: root.controller.progressValue
        Layout.fillWidth: true
    }

    HnLabel {
        objectName: "transactionNotice"
        role: HnTypographyRole.Body
        rawText: root.controller.noticeText
        visible: root.controller.noticeText.length > 0
        wrapMode: Text.Wrap
        color: HoloniightPalette.warning
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.fillWidth: true

        Item {
            Layout.fillWidth: true
        }

        Controls.Button {
            objectName: "transactionRunCancelButton"
            enabled: root.controller.cancelEnabled
            text: root.controller.cancelLabel
            onClicked: root.controller.cancelRun()
        }
    }
}
