pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core

// One row of a plan preview, a hook list or a changed list. Every text is plain text (HnLabel.rawText).
RowLayout {
    id: root

    required property string name
    required property string kind
    required property string action
    required property string oldVersion
    required property string newVersion
    required property string repository
    required property bool conditional

    readonly property string versionText: root.oldVersion.length > 0 && root.newVersion.length > 0
        ? root.oldVersion + " → " + root.newVersion
        : (root.newVersion.length > 0 ? root.newVersion : root.oldVersion)
    readonly property string kindText: {
        switch (root.kind) {
        case "dependency": return qsTr("dependency")
        case "orphan": return qsTr("orphan")
        case "hook": return root.conditional ? qsTr("hook (conditional)") : qsTr("hook")
        case "changed": return root.action === "remove" ? qsTr("removed") : qsTr("installed")
        default: return qsTr("target")
        }
    }

    spacing: 8

    HnLabel {
        objectName: "transactionRowName"
        role: HnTypographyRole.Body
        rawText: root.name
        elide: Text.ElideRight
        color: HoloniightPalette.textPrimary
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }

    HnLabel {
        objectName: "transactionRowKind"
        role: HnTypographyRole.Caption
        rawText: root.kindText
        color: HoloniightPalette.textMuted
    }

    HnLabel {
        objectName: "transactionRowVersion"
        role: HnTypographyRole.Caption
        rawText: root.versionText
        visible: root.versionText.length > 0
        color: HoloniightPalette.textSecondary
    }

    HnLabel {
        objectName: "transactionRowRepository"
        role: HnTypographyRole.Caption
        rawText: root.repository
        visible: root.repository.length > 0
        color: HoloniightPalette.textMuted
    }
}
