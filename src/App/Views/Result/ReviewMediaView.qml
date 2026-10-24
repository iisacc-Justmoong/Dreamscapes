pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtMultimedia
import LVRS 1.0 as LV

Item {
    id: root
    property url source: ""
    property url mediaSource: ""
    property bool video: false
    property bool interactive: false
    property int pixelWidth: 0
    property int pixelHeight: 0
    property int maximumPreviewEdge: 2048
    property real zoom: 0
    property real panX: 0
    property real panY: 0
    property size decodedSize: Qt.size(0,0)
    onSourceChanged: decodedSize = Qt.size(0,0)
    readonly property int imageStatus: image.status
    readonly property int status: image.status
    readonly property int fillMode: image.fillMode
    readonly property real paintedWidth: image.paintedWidth
    readonly property real paintedHeight: image.paintedHeight
    readonly property real sourceWidth: decodedSize.width > 0 ? decodedSize.width : Math.max(1,pixelWidth)
    readonly property real sourceHeight: decodedSize.height > 0 ? decodedSize.height : Math.max(1,pixelHeight)
    readonly property real fitScale: Math.min(viewport.width / sourceWidth, viewport.height / sourceHeight)
    readonly property real effectiveScale: zoom > 0 ? zoom / Screen.devicePixelRatio : fitScale
    readonly property real effectiveZoom: effectiveScale * Screen.devicePixelRatio
    signal zoomRequested(real value)
    signal panRequested(real x, real y)
    signal playbackFailed(string message)
    clip: true
    onVisibleChanged: if (!visible) player.stop()

    Item {
        id: viewport
        anchors.fill: parent
        anchors.bottomMargin: controls.visible ? controls.implicitHeight + LV.Theme.gap4 : 0
        clip: true
        Item {
            id: surface
            width: root.sourceWidth * root.effectiveScale
            height: root.sourceHeight * root.effectiveScale
            x: (viewport.width - width) / 2 - root.panX * Math.max(0, (width - viewport.width) / 2)
            y: (viewport.height - height) / 2 - root.panY * Math.max(0, (height - viewport.height) / 2)
            Image {
                id: image
                objectName: root.objectName + "Image"
                anchors.fill: parent
                source: root.visible ? root.source : ""
                sourceSize.width: Math.max(1,Math.min(root.maximumPreviewEdge,root.interactive
                    ? root.pixelWidth > 0 ? root.pixelWidth : root.maximumPreviewEdge : Math.ceil(viewport.width * Screen.devicePixelRatio)))
                sourceSize.height: Math.max(1,Math.min(root.maximumPreviewEdge,root.interactive
                    ? root.pixelHeight > 0 ? root.pixelHeight : root.maximumPreviewEdge : Math.ceil(viewport.height * Screen.devicePixelRatio)))
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                onStatusChanged: if (status === Image.Ready) {
                    // sourceSize is a decode budget; implicit dimensions retain the original aspect ratio.
                    const scale = Math.min(1,sourceSize.width / Math.max(1,implicitWidth),sourceSize.height / Math.max(1,implicitHeight))
                    root.decodedSize = Qt.size(Math.max(1,Math.round(implicitWidth * scale)),Math.max(1,Math.round(implicitHeight * scale)))
                }
                visible: player.playbackState === MediaPlayer.StoppedState
            }
            VideoOutput {
                id: output
                anchors.fill: parent
                visible: root.video && player.playbackState !== MediaPlayer.StoppedState
                fillMode: VideoOutput.PreserveAspectFit
            }
        }
        LV.Label {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - LV.Theme.gap16)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            style: caption
            visible: player.error !== MediaPlayer.NoError || (image.status !== Image.Ready && player.playbackState === MediaPlayer.StoppedState)
            text: player.error !== MediaPlayer.NoError ? player.errorString : !root.source.toString() ? qsTr("No result selected") : image.status === Image.Error
                ? qsTr("Preview unavailable") : qsTr("Loading preview…")
        }
        WheelHandler {
            enabled: root.interactive
            target: null
            onWheel: function(event) {
                const delta = event.angleDelta.y || event.pixelDelta.y
                if (!delta) { event.accepted = false; return }
                root.zoomRequested(Math.max(0.05, Math.min(16, root.effectiveZoom * (delta > 0 ? 1.2 : 1 / 1.2))))
                event.accepted = true
            }
        }
        DragHandler {
            id: drag
            enabled: root.interactive
            target: null
            property real startX: 0
            property real startY: 0
            onActiveChanged: if (active) { startX = root.panX; startY = root.panY }
            onTranslationChanged: {
                if (!active) return
                const dx = Math.max(1, (surface.width - viewport.width) / 2)
                const dy = Math.max(1, (surface.height - viewport.height) / 2)
                root.panRequested(surface.width > viewport.width ? Math.max(-1, Math.min(1, startX - translation.x / dx)) : 0,
                    surface.height > viewport.height ? Math.max(-1, Math.min(1, startY - translation.y / dy)) : 0)
            }
        }
    }
    MediaPlayer {
        id: player
        objectName: root.objectName + "Player"
        source: root.video && root.visible ? root.mediaSource : ""
        videoOutput: output
        audioOutput: AudioOutput {}
        onErrorOccurred: function(error,message) {
            if (root.video && root.visible && root.mediaSource.toString().length) root.playbackFailed(message)
        }
    }
    LV.HStack {
        id: controls
        anchors.bottom: parent.bottom
        width: parent.width
        spacing: LV.Theme.gap4
        visible: root.video && root.mediaSource.toString().length > 0
        LV.LabelButton {
            objectName: "videoPlayButton"
            text: player.playbackState === MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Play")
            tone: LV.AbstractButton.Default
            onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
        }
        LV.LabelButton {
            objectName: "videoReplayButton"
            text: qsTr("Replay")
            tone: LV.AbstractButton.Default
            onClicked: { player.position = 0; player.play() }
        }
        LV.Label {
            Layout.fillWidth: true
            text: qsTr("%1 / %2 s").arg((player.position / 1000).toFixed(1)).arg((player.duration / 1000).toFixed(1))
            style: caption
            elide: Text.ElideRight
        }
    }
}
