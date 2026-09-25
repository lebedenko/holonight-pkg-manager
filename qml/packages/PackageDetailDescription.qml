import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

HnLabel {
    id: root

    required property string description

    role: HnTypographyRole.Body
    rawText: root.description
    color: HoloniightPalette.textSecondary
    wrapMode: Text.Wrap
    visible: root.description.length > 0
    Layout.fillWidth: true
}
