pragma ComponentBehavior: Bound

import Holonight.Controls

HnListDelegate {
    id: root

    required property string name
    required property string installedVersion
    required property string sourceLabel

    title: root.name
    subtitle: root.installedVersion
    metadata: root.sourceLabel
}
