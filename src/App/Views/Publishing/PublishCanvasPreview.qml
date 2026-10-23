pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Rectangle {
    id: root
    required property var project
    property int sourceIndex: 0
    property int maximumWidth: 1024
    property int maximumHeight: 1024
    property bool requested: visible
    property int completion: 0
    readonly property alias status: artwork.status
    readonly property alias imageSource: artwork.source
    color: LV.Theme.surfaceInput
    radius: LV.Theme.radiusMd
    clip: true
    Connections {
        target: root.project
        function onPagePreviewReady(page) { if (page === root.sourceIndex) ++root.completion }
    }
    Image {
        id: artwork
        objectName: root.objectName + "Image"
        anchors.fill: parent
        source: {
            const revision = root.project.pagePreviewRevision
            const completion = root.completion
            return root.requested ? root.project.pagePreviewSource(root.sourceIndex, root.maximumWidth, root.maximumHeight) : ""
        }
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: false
    }
    LV.Label {
        anchors.centerIn: parent
        width: Math.max(0,parent.width-16)
        horizontalAlignment: Text.AlignHCenter
        style: caption
        visible: artwork.status !== Image.Ready && root.width > 100
        text: artwork.status === Image.Error ? qsTr("Preview unavailable") : qsTr("Preparing preview…")
    }
}
