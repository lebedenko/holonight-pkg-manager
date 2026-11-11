pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core

// The preview: what will change, the hooks that will run, when the confirmation expires, and Confirm / Cancel.
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
        objectName: "transactionNotice"
        role: HnTypographyRole.Body
        rawText: root.controller.noticeText
        visible: root.controller.noticeText.length > 0
        wrapMode: Text.Wrap
        color: HoloniightPalette.warning
        Layout.fillWidth: true
    }

    ListView {
        id: changes

        objectName: "transactionPreviewList"
        model: root.controller.previewRows
        clip: true
        interactive: contentHeight > height
        implicitHeight: Math.min(contentHeight, 220)
        spacing: 6
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight

        delegate: TransactionChangeRow {
            width: ListView.view ? ListView.view.width : implicitWidth
        }
    }

    HnLabel {
        objectName: "transactionHooksHeader"
        role: HnTypographyRole.Caption
        rawText: qsTr("Hooks that will run")
        visible: root.controller.hookRows.count > 0
        color: HoloniightPalette.textMuted
    }

    ListView {
        objectName: "transactionHookList"
        model: root.controller.hookRows
        visible: root.controller.hookRows.count > 0
        clip: true
        interactive: contentHeight > height
        implicitHeight: Math.min(contentHeight, 96)
        spacing: 4
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? implicitHeight : 0

        delegate: TransactionChangeRow {
            width: ListView.view ? ListView.view.width : implicitWidth
        }
    }

    HnLabel {
        objectName: "transactionExpires"
        role: HnTypographyRole.Caption
        rawText: root.controller.expiresText
        wrapMode: Text.Wrap
        color: HoloniightPalette.textMuted
        Layout.fillWidth: true
    }

    TransactionActionRow {
        controller: root.controller
        Layout.fillWidth: true
    }
}
