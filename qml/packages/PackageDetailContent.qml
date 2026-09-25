pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// Common detail layout; callers supply only the fields and sections specific to their page.
ColumnLayout {
    id: root

    required property string name
    required property string sourceLabel
    required property string repository
    required property var metadataRows
    required property string description
    property bool showActions: true
    property bool showInstalledIndicator: true
    property Component extraSections: null
    property Component footerContent: null

    spacing: 16

    PackageDetailHeader {
        name: root.name
        sourceLabel: root.sourceLabel
        repository: root.repository
        showActions: root.showActions
        showInstalledIndicator: root.showInstalledIndicator
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.topMargin: 16
    }

    PackageDetailMetadataRows {
        rows: root.metadataRows
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
    }

    PackageDetailDescription {
        description: root.description
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
    }

    Loader {
        active: root.extraSections !== null
        sourceComponent: root.extraSections
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.bottomMargin: root.footerContent === null ? 16 : 0
    }

    Loader {
        active: root.footerContent !== null
        sourceComponent: root.footerContent
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.bottomMargin: 16
    }
}
