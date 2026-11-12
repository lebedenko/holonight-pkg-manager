pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core

// The preview of a system upgrade (system-upgrade REQ-F-082, REQ-F-083): one scrollable list with a section per kind
// (upgrades, replacements, new packages, held back), the keyring notice, the sizes and the expiry. There is no
// per-package selection: the whole upgrade runs or nothing does.
ColumnLayout {
    id: root

    required property var controller

    spacing: 12

    HnLabel {
        objectName: "transactionTitle"
        role: HnTypographyRole.Title
        rawText: root.controller.title
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
        objectName: "upgradeKeyringNotice"
        role: HnTypographyRole.Body
        rawText: root.controller.keyringNotice
        visible: root.controller.keyringNotice.length > 0
        wrapMode: Text.Wrap
        color: HoloniightPalette.textPrimary
        Layout.fillWidth: true
    }

    HnLabel {
        objectName: "upgradePreviewHeading"
        role: HnTypographyRole.Body
        rawText: root.controller.previewHeading
        color: HoloniightPalette.textSecondary
        Layout.fillWidth: true
    }

    ListView {
        id: rows

        objectName: "upgradePreviewList"
        model: root.controller.previewRows
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        implicitHeight: Math.min(contentHeight, 320)
        spacing: 6
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        Layout.minimumHeight: 0
        Layout.fillHeight: true

        section.property: "kind"
        section.delegate: HnLabel {
            required property string section

            objectName: "upgradeSectionHeader"
            role: HnTypographyRole.Caption
            topPadding: 6
            rawText: {
                switch (section) {
                case "upgrade": return qsTr("Updates")
                case "replacement": return qsTr("Replacements")
                case "newDependency": return qsTr("New packages")
                case "heldBack": return root.controller.heldBackHeading
                default: return ""
                }
            }
            color: HoloniightPalette.textMuted
        }

        Controls.ScrollBar.vertical: Controls.ScrollBar {
            objectName: "upgradePreviewScrollBar"
            policy: rows.contentHeight > rows.height ? Controls.ScrollBar.AlwaysOn : Controls.ScrollBar.AsNeeded
        }

        delegate: TransactionChangeRow {
            width: ListView.view ? ListView.view.width - 12 : implicitWidth
        }
    }

    HnLabel {
        objectName: "upgradeDownloadSize"
        role: HnTypographyRole.Caption
        rawText: root.controller.downloadText
        color: HoloniightPalette.textSecondary
        Layout.fillWidth: true
    }

    HnLabel {
        objectName: "upgradeSizeChange"
        role: HnTypographyRole.Caption
        rawText: root.controller.sizeChangeText
        color: HoloniightPalette.textSecondary
        Layout.fillWidth: true
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
