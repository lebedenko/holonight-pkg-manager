pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core

// The outcome: a refusal, a failure, success, cancelled, unknown or helper unavailable. The message comes from the
// fixed message table via the controller; the changed list shows only when there is something to show.
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
        objectName: "transactionMessage"
        role: HnTypographyRole.Body
        rawText: root.controller.message
        wrapMode: Text.Wrap
        color: HoloniightPalette.textPrimary
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

    HnLabel {
        objectName: "upgradeDisabledReason"
        rawText: root.controller.upgradeDisabledReason
        visible: root.controller.actions.indexOf("updateSystem") >= 0 && rawText.length > 0
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    ListView {
        objectName: "transactionResultList"
        model: root.controller.resultRows
        visible: root.controller.resultRows.count > 0
        clip: true
        interactive: contentHeight > height
        implicitHeight: Math.min(contentHeight, 220)
        spacing: 6
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? implicitHeight : 0

        delegate: TransactionChangeRow {
            width: ListView.view ? ListView.view.width : implicitWidth
        }
    }

    TransactionActionRow {
        controller: root.controller
        Layout.fillWidth: true
    }
}
