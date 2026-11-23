pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// A titled list section of the transaction details, hidden when it has no items (REQ-F-030). Items are the children.
ColumnLayout {
    id: root

    required property string titleText
    required property int count

    spacing: 4
    visible: root.count > 0

    HnSectionHeader {
        titleText: root.titleText
        dividerVisible: false
        Layout.fillWidth: true

        trailingContent: Component {
            HnLabel {
                role: HnTypographyRole.Caption
                rawText: String(root.count)
                color: HoloniightPalette.textSecondary
            }
        }
    }
}
