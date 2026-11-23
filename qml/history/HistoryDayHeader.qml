import QtQuick
import Holonight.Core
import Holonight.Controls
import HolonightPackages

// Section header of one local calendar day (REQ-F-025). `section` is the "YYYY-MM-DD" day key.
Item {
    id: root

    required property string section
    property HistoryModel historyModel: null

    implicitHeight: dayLabel.implicitHeight + 16

    HnLabel {
        id: dayLabel

        objectName: "historyDayHeader"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        role: HnTypographyRole.Caption
        font.bold: true
        rawText: root.historyModel ? root.historyModel.formatDay(root.section) : root.section
        color: HoloniightPalette.textMuted
        elide: Text.ElideRight
    }
}
