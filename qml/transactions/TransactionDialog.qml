pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core

// The only transaction popup (REQ-F-031): one modal card whose page changes with the controller. CloseOnPressOutside
// is deliberately absent so a stray click cannot discard a preview, and Escape works only while the state is closable.
Controls.Popup {
    id: root

    // Null leaves the dialog inert (no transaction view-model was provided).
    property var controller: null

    objectName: "transactionDialog"
    modal: true
    focus: true
    parent: Controls.Overlay.overlay
    anchors.centerIn: Controls.Overlay.overlay
    width: Math.min(520, root.parent ? root.parent.width - 48 : 520)
    padding: 20
    closePolicy: root.controller === null || root.controller.closable ? Controls.Popup.CloseOnEscape : Controls.Popup.NoAutoClose

    Connections {
        target: root.controller

        function onChanged() {
            const wanted = root.controller !== null && root.controller.dialogOpen
            if (wanted && !root.opened) {
                root.open()
            } else if (!wanted && root.opened) {
                root.close()
            }
        }
    }

    Component.onCompleted: {
        if (root.controller !== null && root.controller.dialogOpen) {
            root.open()
        }
    }

    // Escape or any other user dismissal while the controller still shows a dialog.
    onClosed: {
        if (root.controller !== null && root.controller.dialogOpen) {
            root.controller.dismiss()
        }
    }

    contentItem: Loader {
        id: pages

        objectName: "transactionPages"
        sourceComponent: {
            if (root.controller === null) {
                return null
            }
            switch (root.controller.page) {
            case 1: return preparingPage    // preparing
            case 2: return confirmPage      // confirmation
            case 3: return progressPage     // progress
            case 4: return resultPage       // result
            default: return null
            }
        }
    }

    Component {
        id: preparingPage

        ColumnLayout {
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
                color: HoloniightPalette.textSecondary
                Layout.fillWidth: true
            }

            Controls.ProgressBar {
                indeterminate: true
                Layout.fillWidth: true
            }
        }
    }

    Component {
        id: confirmPage

        TransactionConfirmPage {
            controller: root.controller
        }
    }

    Component {
        id: progressPage

        TransactionProgressPage {
            controller: root.controller
        }
    }

    Component {
        id: resultPage

        TransactionResultPage {
            controller: root.controller
        }
    }
}
