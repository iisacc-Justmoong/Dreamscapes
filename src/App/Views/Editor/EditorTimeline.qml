pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

FocusScope {
    id: root
    objectName: "editorTimeline"
    required property var canvas
    property var generation: null
    readonly property EditorTimelineModel stateModel: EditorTimelineModel {
        canvas: root.canvas
    }
    property bool playing: false
    property real timelineScale: 1
    property bool editingTime: false
    property string renameTarget: ""
    // Viewing a short document must not stretch its only frame across the ruler.
    readonly property int viewFrameCount: Math.max(3, stateModel.totalFrames, Math.ceil(stateModel.fps))
    readonly property int firstVisibleFrame: Math.max(0, Math.floor(lanes.contentX / Math.max(1, lanes.contentWidth) * viewFrameCount))
    readonly property int lastVisibleFrame: Math.min(viewFrameCount - 1, Math.ceil((lanes.contentX + lanes.width) / Math.max(1, lanes.contentWidth) * viewFrameCount) - 1)
    readonly property int rulerHeight: 44
    readonly property color rulerTickColor: "#8d8d8d"
    readonly property color rulerBaselineColor: "#f0f000"
    readonly property color rulerPlayheadColor: "#4096f3"
    readonly property bool modalActive: imageDialog.visible
    readonly property int gutter: width < 600 ? 88 : width < 1100 ? 144 : 184
    readonly property bool wide: width >= 900
    readonly property var selected: stateModel.selectedClip
    readonly property var selectedKey: stateModel.selectedKey
    readonly property var trackLayout: {
        let top = rulerHeight;
        return stateModel.tracks.filter(track => track.type === "Audio" || stateModel.filter === "All" || stateModel.clips.some(clip => clip.trackId === track.id && (stateModel.filter === "Out" ? clip.excluded : !clip.excluded))).map(track => {
            const curve = stateModel.mode === "Curves" && stateModel.keys.some(key => key.trackId === track.id && key.kind === "Motion");
            const height = (track.type === "Audio" ? 48 : 40) + (curve ? 64 : 0);
            const row = Object.assign({}, track, {
                top: top,
                height: height,
                curve: curve
            });
            top += height;
            return row;
        });
    }
    readonly property real laneHeight: rulerHeight + (trackLayout.length ? trackLayout.reduce((sum, track) => sum + track.height, 0) : 82)
    readonly property int tickStep: {
        const needed = viewFrameCount / Math.max(1, lanes.contentWidth / 76), power = Math.pow(10, Math.floor(Math.log10(Math.max(1, needed))));
        return [1, 2, 5, 10].map(value => value * power).find(value => value >= needed) || 1;
    }
    readonly property int minorTickStep: tickStep % 5 === 0 ? tickStep / 5 : tickStep % 2 === 0 ? tickStep / 2 : tickStep
    implicitHeight: footer.y + footer.height + 1
    signal previewError(string message)
    function frameLabel(frame) {
        return "F" + String(frame).padStart(3, "0");
    }
    function number(value) {
        const match = String(value).match(/-?\d+/);
        return match ? Number(match[0]) : NaN;
    }
    function applyRange(first, last) {
        if (Number.isInteger(first) && Number.isInteger(last))
            stateModel.setRange(selected.id, first, last);
    }
    function seekFrame(frame) {
        playing = false;
        stateModel.seek(frame);
    }
    function fitTimeline() {
        timelineScale = 1;
        lanes.contentX = 0;
        viewport.contentY = 0;
    }
    function timecode() {
        const rate = Math.max(1, Math.round(stateModel.fps)), frame = stateModel.playhead;
        return [Math.floor(frame / rate / 3600), Math.floor(frame / rate / 60) % 60, Math.floor(frame / rate) % 60, frame % rate].map(value => String(value).padStart(2, "0")).join(":") + " · " + frameLabel(frame);
    }
    function rowSpacer(row, spacer) {
        const items = row.children.filter(item => item !== spacer && item.visible && item.width > 0);
        return root.wide ? Math.max(0, row.width - items.reduce((sum, item) => sum + item.width, 0) - row.spacing * items.length) : 0;
    }
    function dragLayer(id, delta, edge) {
        stateModel.dragClip(id, delta, edge || "", Math.max(0, Math.floor(6 / lanes.contentWidth * viewFrameCount)));
    }
    function chooseFrame(id) {
        imageDialog.targetCanvas = canvas;
        imageDialog.targetLayer = id;
        imageDialog.targetFrame = stateModel.playhead;
        imageDialog.open();
    }
    onVisibleChanged: if (!visible)
        playing = false
    onEnabledChanged: if (!enabled)
        playing = false
    onCanvasChanged: {
        playing = false;
        Qt.callLater(fitTimeline);
    }
    Connections {
        target: root.canvas
        function onDocumentChanged() { root.playing = false; }
    }
    onPlayingChanged: {
        if (!canvas || !canvas.media)
            return;
        if (!playing)
            canvas.media.stop();
        else if (stateModel.tracks.some(track => track.type === "Audio") && !canvas.playTimelineAudio()) {
            playing = false;
            previewError(canvas.error);
        }
    }
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Space) {
            if (stateModel.totalFrames > 1)
                playing = !playing;
            event.accepted = true;
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            seekFrame(stateModel.playhead + (event.key === Qt.Key_Left ? -1 : 1));
            event.accepted = true;
        } else if (event.key === Qt.Key_V || event.key === Qt.Key_B) {
            stateModel.tool = event.key === Qt.Key_V ? "Select" : "Blade";
            event.accepted = true;
        } else if (event.key === Qt.Key_I) {
            stateModel.markIn = stateModel.playhead;
            event.accepted = true;
        } else if (event.key === Qt.Key_O) {
            stateModel.markOut = stateModel.playhead + 1;
            event.accepted = true;
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            if (selectedKey.id)
                stateModel.removeKey(selectedKey.id);
            else
                stateModel.removeSelected();
            event.accepted = true;
        } else if (event.key === Qt.Key_Z && (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) {
            if (event.modifiers & Qt.ShiftModifier)
                stateModel.redo();
            else
                stateModel.undo();
            event.accepted = true;
        }
    }
    Timer {
        interval: Math.max(1, 1000 / root.stateModel.fps)
        repeat: true
        running: root.playing && root.visible && root.enabled
        property int firstFrame: 0
        property double startedAt: 0
        onRunningChanged: if (running) {
            firstFrame = root.stateModel.playhead;
            startedAt = Date.now();
        }
        onTriggered: {
            const frame = firstFrame + Math.floor((Date.now() - startedAt) / 1000 * root.stateModel.fps);
            root.stateModel.seek(frame);
            if (frame >= root.stateModel.totalFrames - 1)
                root.playing = false;
        }
    }
    component Button: LV.LabelButton {
        id: button
        property bool selected: false
        property bool selectable: false
        height: 22
        verticalPadding: 2
        horizontalPadding: 8
        tone: LV.AbstractButton.Default
        EditorButtonState {
            button: button
            selectable: button.selectable
            selected: button.selected
        }
    }
    component ToolButton: LV.IconButton {
        id: button
        property bool selected: false
        property bool selectable: false
        property bool flipIcon: false
        width: 22
        height: 22
        iconSize: 18
        horizontalPadding: 2
        verticalPadding: 2
        tone: LV.AbstractButton.Default
        transform: Scale {
            origin.x: button.width / 2
            xScale: button.flipIcon ? -1 : 1
        }
        EditorButtonState {
            button: button
            selectable: button.selectable
            selected: button.selected
        }
        Controls.ToolTip.visible: hovered
        Controls.ToolTip.text: Accessible.name
        Controls.ToolTip.delay: 500
    }
    component Field: LV.InputField {
        height: 24
        width: 92
        clearButtonVisible: false
    }
    Flickable {
        id: viewport
        objectName: "editorTimelineViewport"
        anchors.fill: parent
        contentWidth: width
        contentHeight: root.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Controls.ScrollBar.vertical: Controls.ScrollBar {
            policy: Controls.ScrollBar.AsNeeded
        }
        Item {
            id: panel
            width: viewport.width
            height: root.implicitHeight
            Row {
                id: header
                objectName: "editorTimelineHeader"
                clip: true
                x: 8
                y: 7
                width: parent.width - 16
                spacing: 8
                Item {
                    width: 156
                    height: 22
                    Button {
                        anchors.fill: parent
                        visible: !root.editingTime
                        tone: LV.AbstractButton.Borderless
                        text: root.timecode()
                        Accessible.name: qsTr("Go to frame")
                        onClicked: {
                            root.editingTime = true;
                            timeInput.forceActiveFocus();
                        }
                    }
                    Field {
                        id: timeInput
                        anchors.fill: parent
                        visible: root.editingTime
                        text: root.frameLabel(root.stateModel.playhead)
                        Accessible.name: qsTr("Go to frame")
                        onAccepted: function (value) {
                            const frame = root.number(value);
                            if (Number.isFinite(frame))
                                root.seekFrame(frame);
                            root.editingTime = false;
                        }
                    }
                }
                Button {
                    text: "‹<"
                    tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("First frame")
                    onClicked: root.seekFrame(0)
                }
                Button {
                    text: "‹"
                    tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Previous cut")
                    onClicked: {
                        root.playing = false;
                        root.stateModel.jumpCut(-1);
                    }
                }
                Button {
                    objectName: "editorTimelinePlay"
                    text: root.playing ? "Ⅱ" : "▶"
                    enabled: root.stateModel.totalFrames > 1
                    Accessible.name: root.playing ? qsTr("Pause timeline") : qsTr("Play timeline")
                    onClicked: root.playing = !root.playing
                }
                Button {
                    text: "›"
                    tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Next cut")
                    onClicked: {
                        root.playing = false;
                        root.stateModel.jumpCut(1);
                    }
                }
                Button {
                    text: ">›"
                    tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Last frame")
                    onClicked: root.seekFrame(root.stateModel.totalFrames - 1)
                }
                Button {
                    text: "−1f"
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.seekFrame(root.stateModel.playhead - 1)
                }
                Button {
                    text: "+1f"
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.seekFrame(root.stateModel.playhead + 1)
                }
                Repeater {
                    model: ["Layers", "Keyframes", "Curves"]
                    delegate: Button {
                        required property string modelData
                        objectName: "editorTimelineMode-" + modelData
                        text: modelData
                        selectable: true
                        selected: root.stateModel.mode === modelData
                        onClicked: root.stateModel.mode = modelData
                    }
                }
                Item {
                    id: headerSpacer
                    width: root.rowSpacer(header, headerSpacer)
                    height: 22
                }
                ToolButton {
                    objectName: "editorTimelineUndo"
                    iconName: "reset"
                    Accessible.name: qsTr("Undo")
                    enabled: root.stateModel.canUndo
                    onClicked: root.stateModel.undo()
                }
                ToolButton {
                    objectName: "editorTimelineRedo"
                    iconName: "reset"
                    flipIcon: true
                    Accessible.name: qsTr("Redo")
                    enabled: root.stateModel.canRedo
                    onClicked: root.stateModel.redo()
                }
                Button {
                    text: qsTr("Split at head")
                    enabled: !!root.selected.id && root.stateModel.playhead > root.selected.in && root.stateModel.playhead < root.selected.out && !root.selected.locked
                    onClicked: root.stateModel.split()
                }
                Button {
                    text: qsTr("Exclude")
                    enabled: !!root.selected.id && !root.selected.locked
                    onClicked: root.stateModel.setClipVisible(root.selected.id, root.selected.excluded)
                }
                Button {
                    text: qsTr("Ripple delete")
                    enabled: !!root.selected.id && !root.selected.locked
                    onClicked: root.stateModel.rippleDelete()
                }
                LV.ComboBox {
                    objectName: "editorTimelineAddLayer"
                    enabled: root.canvas && root.canvas.documentReady
                    width: 86
                    height: 22
                    arrow: LV.Stepper.Down
                    text: qsTr("Layer +")
                    onClicked: addMenu.openFor(this, 0, height)
                }
                Button {
                    text: "···"
                    tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Timeline actions")
                    onClicked: moreMenu.openFor(this, 0, height)
                }
            }
            Row {
                id: tools
                objectName: "editorTimelineTools"
                clip: true
                x: 8
                y: header.y + header.height + 10
                width: parent.width - 16
                spacing: 4
                Repeater {
                    model: [
                        {
                            name: "Select",
                            icon: "direct-select"
                        },
                        {
                            name: "Blade",
                            icon: "outline-stroke"
                        },
                        {
                            name: "Trim",
                            icon: "generalcut"
                        },
                        {
                            name: "Ripple",
                            icon: "ripple-edit"
                        },
                        {
                            name: "Roll",
                            icon: "roll-edit"
                        },
                        {
                            name: "Slip",
                            icon: "slip-edit"
                        },
                        {
                            name: "Slide",
                            icon: "slide-edit"
                        },
                        {
                            name: "Range",
                            icon: "panel-trim-range"
                        }
                    ]
                    delegate: ToolButton {
                        required property var modelData
                        objectName: "editorTimelineTool-" + modelData.name
                        iconName: modelData.icon
                        Accessible.name: modelData.name
                        selectable: true
                        selected: root.stateModel.tool === modelData.name
                        enabled: root.stateModel.canvas && root.stateModel.toolAvailable(modelData.name)
                        onClicked: root.stateModel.tool = modelData.name
                    }
                }
                Button {
                    text: "I In"
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.stateModel.markIn = root.stateModel.playhead
                }
                Button {
                    text: "O Out"
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.stateModel.markOut = root.stateModel.playhead + 1
                }
                Item {
                    id: toolSpacer
                    width: root.rowSpacer(tools, toolSpacer)
                    height: 22
                }
                Repeater {
                    model: ["All", "Kept", "Out"]
                    delegate: Button {
                        required property string modelData
                        text: modelData + " " + root.stateModel.clips.filter(clip => clip.type === "Layer" && (modelData === "All" || (modelData === "Out" ? clip.excluded : !clip.excluded))).length
                        selectable: true
                        selected: root.stateModel.filter === modelData
                        onClicked: root.stateModel.filter = modelData
                    }
                }
                ToolButton {
                    iconName: "snap"
                    Accessible.name: qsTr("Snap to edits")
                    selectable: true
                    selected: root.stateModel.snap
                    onClicked: root.stateModel.snap = !root.stateModel.snap
                }
                ToolButton {
                    iconName: "include-editor"
                    Accessible.name: qsTr("Move selected clips together")
                    selectable: true
                    selected: root.stateModel.linked
                    onClicked: root.stateModel.linked = !root.stateModel.linked
                }
                LV.ComboBox {
                    objectName: "editorTimelineZoom"
                    width: 78
                    height: 22
                    arrow: LV.Stepper.Down
                    text: Math.round(root.timelineScale * 100) + "%"
                    onClicked: zoomMenu.openFor(this, 0, height)
                }
                Button {
                    objectName: "editorTimelineFit"
                    text: qsTr("Fit")
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.fitTimeline()
                }
            }
            readonly property real rulerY: tools.y + tools.height + 3
            Row {
                x: 4
                y: panel.rulerY
                width: root.gutter - 8
                height: root.rulerHeight
                spacing: 3
                ToolButton {
                    height: root.rulerHeight
                    iconName: "frame"
                    tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Frame rate")
                    onClicked: fpsMenu.openFor(this, 0, height)
                }
                LV.Label {
                    height: root.rulerHeight
                    width: parent.width - 25
                    verticalAlignment: Text.AlignVCenter
                    text: Number(root.stateModel.fps.toFixed(3)) + (root.gutter < 100 ? " fps" : " fps · frame number")
                    style: body
                    elide: Text.ElideRight
                }
            }
            Repeater {
                model: root.trackLayout
                delegate: Item {
                    id: heading
                    required property var modelData
                    x: 0
                    y: panel.rulerY + modelData.top
                    width: root.gutter
                    height: modelData.height
                    Row {
                        width: parent.width
                        height: parent.height
                        Button {
                            width: root.gutter - 66
                            height: Math.min(40, heading.height)
                            text: heading.modelData.type
                            textColor: LV.Theme.textPrimary
                            horizontalPadding: 4
                            contentItem: LV.Label {
                                text: heading.modelData.type
                                color: LV.Theme.textPrimary
                                style: body
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            tone: LV.AbstractButton.Borderless
                            clip: true
                            Accessible.name: heading.modelData.name
                            onClicked: {
                                root.stateModel.selectTrack(heading.modelData.id);
                                trackMenu.targetId = heading.modelData.id;
                                trackMenu.openFor(this, 0, height);
                            }
                        }
                        Button {
                            width: 22
                            height: Math.min(40, heading.height)
                            horizontalPadding: 0
                            text: "L"
                            tone: LV.AbstractButton.Borderless
                            selectable: true
                            selected: heading.modelData.locked
                            Accessible.name: qsTr("Lock %1").arg(heading.modelData.name)
                            onClicked: root.stateModel.setTrackLocked(heading.modelData.id, !heading.modelData.locked)
                        }
                        Button {
                            width: 22
                            height: Math.min(40, heading.height)
                            horizontalPadding: 0
                            text: "S"
                            tone: LV.AbstractButton.Borderless
                            selectable: true
                            selected: heading.modelData.solo
                            Accessible.name: qsTr("Solo %1").arg(heading.modelData.name)
                            onClicked: root.stateModel.setTrackSolo(heading.modelData.id)
                        }
                        Button {
                            width: 22
                            height: Math.min(40, heading.height)
                            horizontalPadding: 0
                            text: "M"
                            tone: LV.AbstractButton.Borderless
                            selectable: true
                            selected: heading.modelData.muted
                            Accessible.name: qsTr("Mute %1").arg(heading.modelData.name)
                            onClicked: root.stateModel.setTrackMuted(heading.modelData.id, !heading.modelData.muted)
                        }
                    }
                }
            }
            Flickable {
                id: lanes
                objectName: "editorTimelineLanes"
                x: root.gutter
                y: panel.rulerY
                width: parent.width - x - 1
                height: root.laneHeight
                contentWidth: width * root.timelineScale
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                Controls.ScrollBar.horizontal: Controls.ScrollBar {
                    policy: Controls.ScrollBar.AsNeeded
                }
                Item {
                    id: laneContent
                    width: lanes.contentWidth
                    height: lanes.height
                    Repeater {
                        model: Math.ceil(root.viewFrameCount / root.tickStep)
                        delegate: Item {
                            required property int index
                            objectName: "editorTimelineRulerTick-" + index * root.tickStep
                            x: index * root.tickStep / root.viewFrameCount * laneContent.width
                            width: 1
                            height: laneContent.height
                            Rectangle {
                                objectName: "editorTimelineRulerMark-" + index * root.tickStep
                                y: 25
                                width: 2
                                height: 14
                                color: root.rulerTickColor
                            }
                            Rectangle {
                                y: root.rulerHeight
                                width: 1
                                height: parent.height - y
                                color: LV.Theme.surfaceInput
                            }
                            LV.Label {
                                objectName: "editorTimelineRulerLabel-" + index * root.tickStep
                                x: 2
                                y: 1
                                text: root.frameLabel(index * root.tickStep)
                                style: caption
                                color: LV.Theme.textSecondary
                            }
                        }
                    }
                    Repeater {
                        model: Math.ceil(root.viewFrameCount / root.minorTickStep)
                        delegate: Rectangle {
                            required property int index
                            readonly property int frame: index * root.minorTickStep
                            objectName: "editorTimelineRulerMinor-" + frame
                            visible: frame % root.tickStep !== 0
                            x: frame / root.viewFrameCount * laneContent.width
                            y: 29
                            width: 2
                            height: 10
                            color: root.rulerTickColor
                        }
                    }
                    Rectangle {
                        objectName: "editorTimelineRulerBaseline"
                        y: 40
                        width: laneContent.width
                        height: 4
                        color: root.rulerBaselineColor
                    }
                    MouseArea {
                        width: parent.width
                        height: root.rulerHeight
                        onPressed: function (mouse) {
                            root.seekFrame(Math.floor(mouse.x / width * root.viewFrameCount));
                        }
                        onPositionChanged: function (mouse) {
                            if (pressed)
                                root.seekFrame(Math.floor(mouse.x / width * root.viewFrameCount));
                        }
                    }
                    LV.Label {
                        objectName: "editorTimelineEmptyState"
                        x: 8
                        y: root.rulerHeight
                        width: parent.width - 16
                        height: 80
                        visible: root.stateModel.tracks.length === 0
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("No layers")
                        style: body
                        color: LV.Theme.textSecondary
                    }
                    Repeater {
                        model: root.trackLayout
                        delegate: Item {
                            id: trackRow
                            required property var modelData
                            objectName: "editorTimelineRow-" + modelData.id
                            y: modelData.top
                            width: laneContent.width
                            height: modelData.height
                            readonly property bool inViewport: lanes.y + y + height >= viewport.contentY && lanes.y + y <= viewport.contentY + viewport.height
                            Repeater {
                                model: root.stateModel.clips.filter(clip => clip.trackId === trackRow.modelData.id && (clip.type === "Audio" || root.stateModel.filter === "All" || (root.stateModel.filter === "Out" ? clip.excluded : !clip.excluded)))
                                delegate: Rectangle {
                                    id: card
                                    required property var modelData
                                    objectName: "editorTimelineClip-" + modelData.id
                                    x: modelData.in / root.viewFrameCount * trackRow.width
                                    y: 4
                                    width: Math.max(1, (modelData.out - modelData.in) / root.viewFrameCount * trackRow.width - 4)
                                    height: modelData.type === "Audio" ? 40 : 32
                                    radius: 8
                                    clip: true
                                    color: LV.Theme.surfaceControl
                                    opacity: modelData.excluded ? 0.4 : 1
                                    border.width: root.stateModel.selectedIds.indexOf(modelData.id) >= 0 ? 1 : 0
                                    border.color: LV.Theme.accent
                                    Image {
                                        id: preview
                                        objectName: "editorTimelineThumbnail-" + card.modelData.id
                                        x: 4
                                        y: 2
                                        width: 32
                                        height: 28
                                        visible: card.modelData.type === "Layer" && card.width > 44
                                        source: visible && trackRow.inViewport && root.visible ? root.stateModel.thumbnail(card.modelData.id, root.stateModel.playhead) : ""
                                        fillMode: Image.PreserveAspectCrop
                                    }
                                    LV.Label {
                                        x: card.modelData.type === "Layer" && card.width > 44 ? 44 : 4
                                        y: 2
                                        width: Math.max(0, card.width - x - 4)
                                        text: card.modelData.name
                                        style: body
                                        elide: Text.ElideRight
                                    }
                                    LV.Label {
                                        x: card.width > 44 ? 44 : 4
                                        y: 17
                                        width: Math.max(0, card.width - x - 4)
                                        visible: card.modelData.type === "Layer"
                                        text: root.frameLabel(card.modelData.in) + " · " + card.modelData.timing + " · " + card.modelData.frames + "f"
                                        style: caption
                                        color: LV.Theme.textSecondary
                                        elide: Text.ElideRight
                                    }
                                    Canvas {
                                        id: wave
                                        x: 2
                                        y: 15
                                        width: Math.max(0, card.width - 4)
                                        height: 23
                                        visible: card.modelData.type === "Audio"
                                        onWidthChanged: requestPaint()
                                        onPaint: {
                                            const values = card.modelData.waveform || [], c = getContext("2d");
                                            c.clearRect(0, 0, width, height);
                                            c.strokeStyle = LV.Theme.accentGreen;
                                            c.lineWidth = 1;
                                            c.beginPath();
                                            values.forEach((value, index) => {
                                                const x = index / Math.max(1, values.length - 1) * width, peak = value * height / 2;
                                                c.moveTo(x, height / 2 - peak);
                                                c.lineTo(x, height / 2 + peak);
                                            });
                                            c.stroke();
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        property real pressX: 0
                                        onPressed: function (mouse) {
                                            root.stateModel.selectClip(card.modelData.id, Boolean(mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)));
                                            pressX = mapToItem(laneContent, mouse.x, 0).x;
                                            if (mouse.button === Qt.RightButton) {
                                                trackMenu.targetId = card.modelData.trackId;
                                                trackMenu.openFor(card, mouse.x, mouse.y);
                                            }
                                        }
                                        onReleased: function (mouse) {
                                            if (mouse.button !== Qt.LeftButton || card.modelData.locked)
                                                return;
                                            const delta = Math.round((mapToItem(laneContent, mouse.x, 0).x - pressX) / laneContent.width * root.viewFrameCount);
                                            if (root.stateModel.tool === "Blade") {
                                                root.seekFrame(Math.floor(pressX / laneContent.width * root.viewFrameCount));
                                                root.stateModel.split();
                                            } else if (delta) {
                                                root.dragLayer(card.modelData.id, delta, "");
                                            }
                                        }
                                        onDoubleClicked: if (card.modelData.type === "Layer" && card.modelData.timing === "Dynamic" && card.modelData.representation === "Bitmap")
                                            root.chooseFrame(card.modelData.id)
                                    }
                                    Repeater {
                                        model: root.stateModel.selectedIds.indexOf(card.modelData.id) >= 0 && !card.modelData.locked ? ["in", "out"] : []
                                        delegate: Rectangle {
                                            id: handle
                                            required property string modelData
                                            x: modelData === "in" ? 0 : card.width - 3
                                            y: 4
                                            width: 3
                                            height: card.height - 8
                                            color: LV.Theme.accent
                                            MouseArea {
                                                anchors.fill: parent
                                                anchors.margins: -3
                                                cursorShape: Qt.SizeHorCursor
                                                property real pressX: 0
                                                onPressed: function (mouse) {
                                                    pressX = mapToItem(laneContent, mouse.x, 0).x;
                                                }
                                                onReleased: function (mouse) {
                                                    root.dragLayer(card.modelData.id, Math.round((mapToItem(laneContent, mouse.x, 0).x - pressX) / laneContent.width * root.viewFrameCount), handle.modelData);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: root.stateModel.mode !== "Layers" ? root.stateModel.keys.filter(key => key.trackId === trackRow.modelData.id) : []
                                delegate: Rectangle {
                                    required property var modelData
                                    x: modelData.frame / root.viewFrameCount * trackRow.width - 3
                                    y: 1
                                    width: 6
                                    height: 6
                                    rotation: 45
                                    color: root.selectedKey.id === modelData.id ? LV.Theme.accent : LV.Theme.titleHeaderColor
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        onClicked: root.stateModel.selectKey(parent.modelData.id)
                                    }
                                }
                            }
                            Canvas {
                                id: curve
                                y: 40
                                width: trackRow.width
                                height: 64
                                visible: trackRow.modelData.curve
                                Connections {
                                    target: root.stateModel
                                    function onChanged() {
                                        curve.requestPaint();
                                    }
                                }
                                onWidthChanged: requestPaint()
                                onPaint: {
                                    const c = getContext("2d");
                                    c.clearRect(0, 0, width, height);
                                    c.strokeStyle = LV.Theme.accent;
                                    c.lineWidth = 2;
                                    c.beginPath();
                                    const keys = root.stateModel.keys.filter(key => key.trackId === trackRow.modelData.id && key.kind === "Motion").sort((a, b) => a.frame - b.frame);
                                    keys.forEach((key, index) => {
                                        const x = key.frame / root.viewFrameCount * width, y = (1 - key.value) * height;
                                        if (!index)
                                            c.moveTo(x, y);
                                        else if (keys[index - 1].interpolation === "Hold") {
                                            c.lineTo(x, (1 - keys[index - 1].value) * height);
                                            c.lineTo(x, y);
                                        } else if (keys[index - 1].interpolation === "Smooth") {
                                            const previous = keys[index - 1], px = previous.frame / root.viewFrameCount * width, py = (1 - previous.value) * height;
                                            c.bezierCurveTo(px + (x - px) / 3, py, px + 2 * (x - px) / 3, y, x, y);
                                        } else
                                            c.lineTo(x, y);
                                    });
                                    c.stroke();
                                }
                            }
                        }
                    }
                    Rectangle {
                        x: root.stateModel.markIn / root.viewFrameCount * laneContent.width
                        y: root.rulerHeight
                        width: (root.stateModel.markOut - root.stateModel.markIn) / root.viewFrameCount * laneContent.width
                        height: parent.height - y
                        color: LV.Theme.accent
                        opacity: root.stateModel.tool === "Range" ? 0.12 : 0
                    }
                    MouseArea {
                        anchors.fill: parent
                        z: 5
                        enabled: root.stateModel.tool === "Range"
                        property int first: 0
                        onPressed: function (mouse) {
                            first = Math.floor(mouse.x / width * root.viewFrameCount);
                            root.stateModel.markRange(first, first);
                        }
                        onPositionChanged: function (mouse) {
                            if (pressed)
                                root.stateModel.markRange(first, Math.floor(mouse.x / width * root.viewFrameCount));
                        }
                        onReleased: root.stateModel.selectMarkedRange()
                    }
                    Rectangle {
                        objectName: "editorTimelinePlayhead"
                        x: root.stateModel.playhead / root.viewFrameCount * laneContent.width
                        y: 16
                        z: 10
                        width: 2
                        height: parent.height - y
                        color: root.rulerPlayheadColor
                    }
                    Canvas {
                        objectName: "editorTimelinePlayheadBadge"
                        x: root.stateModel.playhead / root.viewFrameCount * laneContent.width + 1 - width / 2
                        y: 16
                        z: 10
                        width: 22
                        height: 26
                        property color markerColor: root.rulerPlayheadColor
                        Accessible.name: qsTr("Playhead at %1").arg(root.frameLabel(root.stateModel.playhead))
                        onMarkerColorChanged: requestPaint()
                        onPaint: {
                            const c = getContext("2d");
                            c.clearRect(0, 0, width, height);
                            c.fillStyle = markerColor;
                            c.beginPath();
                            c.moveTo(0, 0);
                            c.lineTo(width, 0);
                            c.lineTo(width, 16);
                            c.lineTo(width / 2, height);
                            c.lineTo(0, 16);
                            c.closePath();
                            c.fill();
                        }
                    }
                }
            }
            LV.Label {
                x: 4
                y: precision.y + 3
                width: root.gutter - 8
                visible: root.wide
                text: root.selected.id ? qsTr("Layer / %1 selected").arg(root.stateModel.selectedIds.length) : qsTr("Layer selection")
                style: body
                elide: Text.ElideRight
            }
            Row {
                id: precision
                objectName: "editorTimelinePrecision"
                clip: true
                x: root.wide ? root.gutter : 8
                y: lanes.y + lanes.height + 3
                width: parent.width - x - 8
                spacing: 4
                Field {
                    objectName: "editorTimelineIn"
                    placeholderText: "In"
                    text: root.selected.id ? "In " + root.frameLabel(root.selected.in) : ""
                    enabled: !!root.selected.id && !root.selected.locked
                    Accessible.name: qsTr("Layer in frame")
                    onAccepted: function (value) {
                        root.applyRange(root.number(value), root.selected.out);
                    }
                }
                Field {
                    objectName: "editorTimelineOut"
                    width: 96
                    placeholderText: "Out"
                    text: root.selected.id ? "Out " + root.frameLabel(root.selected.out) : ""
                    enabled: !!root.selected.id && !root.selected.locked
                    Accessible.name: qsTr("Layer out frame, exclusive")
                    onAccepted: function (value) {
                        root.applyRange(root.selected.in, root.number(value));
                    }
                }
                Field {
                    objectName: "editorTimelineDuration"
                    width: 88
                    placeholderText: "Duration"
                    text: root.selected.id ? root.selected.frames + "f" : ""
                    enabled: !!root.selected.id && !root.selected.locked
                    Accessible.name: qsTr("Layer duration in frames")
                    onAccepted: function (value) {
                        root.applyRange(root.selected.in, root.selected.in + root.number(value));
                    }
                }
                Field {
                    objectName: "editorTimelineSource"
                    width: 116
                    placeholderText: "Source"
                    text: root.selected.id ? "Source " + root.frameLabel(root.selected.sourceFrame) : ""
                    enabled: !!root.selected.sourceEditable && !root.selected.locked
                    Accessible.name: qsTr("Source frame")
                    onAccepted: function (value) {
                        const frame = root.number(value);
                        if (Number.isInteger(frame)) root.stateModel.setSourceFrame(root.selected.id, frame);
                    }
                }
                Repeater {
                    model: [
                        {
                            edge: "in",
                            delta: -1
                        },
                        {
                            edge: "in",
                            delta: 1
                        },
                        {
                            edge: "out",
                            delta: -1
                        },
                        {
                            edge: "out",
                            delta: 1
                        }
                    ]
                    delegate: Button {
                        required property var modelData
                        height: 24
                        text: (modelData.edge === "in" ? "In " : "Out ") + (modelData.delta < 0 ? "−1f" : "+1f")
                        enabled: !!root.selected.id && !root.selected.locked
                        onClicked: root.stateModel.trim(root.selected.id, modelData.edge, modelData.delta)
                    }
                }
                LV.Label {
                    height: 24
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Out exclusive")
                    style: caption
                    color: LV.Theme.textSecondary
                }
                Button {
                    height: 24
                    text: qsTr("Keep")
                    enabled: !!root.selected.id && !root.selected.locked
                    onClicked: root.stateModel.setClipVisible(root.selected.id, true)
                }
                Button {
                    height: 24
                    text: qsTr("Exclude")
                    enabled: !!root.selected.id && !root.selected.locked
                    onClicked: root.stateModel.setClipVisible(root.selected.id, false)
                }
                Button {
                    height: 24
                    text: qsTr("Solo")
                    enabled: !!root.selected.id
                    onClicked: root.stateModel.setTrackSolo(root.selected.trackId)
                }
                Button {
                    height: 24
                    text: qsTr("Lock")
                    enabled: !!root.selected.id
                    onClicked: root.stateModel.setTrackLocked(root.selected.trackId, !root.selected.locked)
                }
                Button {
                    height: 24
                    text: qsTr("Duplicate")
                    enabled: !!root.selected.id && !root.selected.locked
                    onClicked: root.stateModel.duplicateSelected()
                }
                Field {
                    visible: !!root.selectedKey.id
                    text: root.selectedKey.id ? "Key " + root.frameLabel(root.selectedKey.frame) : ""
                    onAccepted: function (value) {
                        root.stateModel.moveKey(root.selectedKey.id, root.number(value));
                    }
                }
                Field {
                    visible: root.selectedKey.kind === "Motion"
                    text: root.selectedKey.id ? String(root.selectedKey.value) : ""
                    onAccepted: function (value) {
                        root.stateModel.setKeyOpacity(root.selectedKey.id, Number(value));
                    }
                }
                Button {
                    visible: !!root.selectedKey.id
                    text: qsTr("Delete key")
                    onClicked: root.stateModel.removeKey(root.selectedKey.id)
                }
                Field {
                    visible: root.renameTarget.length > 0
                    placeholderText: qsTr("Layer name")
                    onAccepted: function (value) {
                        root.stateModel.renameTrack(root.renameTarget, value);
                        root.renameTarget = "";
                    }
                }
            }
            LV.Label {
                id: errorLabel
                x: 4
                y: precision.y + precision.height + 3
                width: parent.width - 8
                visible: root.stateModel.error.length > 0
                text: root.stateModel.error
                style: caption
                wrapMode: Text.Wrap
                color: LV.Theme.accentRed
            }
            Row {
                id: footer
                objectName: "editorTimelineFooter"
                clip: true
                x: 4
                y: precision.y + precision.height + 6 + (errorLabel.visible ? errorLabel.height + 3 : 0)
                width: parent.width - 8
                spacing: 8
                LV.Label {
                    height: 24
                    width: Math.min(432, footer.width)
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("%1 layers · %2 kept · %3f · %4 keys").arg(root.stateModel.clips.filter(clip => clip.type === "Layer").length).arg(root.stateModel.clips.filter(clip => clip.type === "Layer" && !clip.excluded).length).arg(root.stateModel.totalFrames).arg(root.stateModel.keys.length)
                    style: body
                    elide: Text.ElideRight
                }
                Item {
                    id: footerSpacer
                    width: root.rowSpacer(footer, footerSpacer)
                    height: 24
                }
                LV.Label {
                    height: 24
                    width: Math.min(360, footer.width)
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("View %1–%2 · %3 s · Snap %4").arg(root.frameLabel(root.firstVisibleFrame)).arg(root.frameLabel(root.lastVisibleFrame)).arg(((root.lastVisibleFrame - root.firstVisibleFrame + 1) / root.stateModel.fps).toFixed(2)).arg(root.stateModel.snap ? "1f" : "off")
                    style: body
                    elide: Text.ElideRight
                }
            }
        }
    }
    LV.ContextMenu {
        id: addMenu
        objectName: "editorTimelineAddMenu"
        items: [qsTr("Static bitmap"), qsTr("Dynamic bitmap"), qsTr("Static vector"), qsTr("Dynamic vector"), qsTr("Import layer…"), qsTr("Audio track")]
        showIconSlot: false
        onItemTriggered: function (index) {
            if (index < 4)
                root.stateModel.addLayer(["Static bitmap", "Dynamic bitmap", "Static vector", "Dynamic vector"][index]);
            else if (index === 4) {
                imageDialog.targetCanvas = root.canvas;
                imageDialog.targetLayer = "";
                imageDialog.open();
            } else
                root.stateModel.addAudioTrack();
        }
    }
    LV.ContextMenu {
        id: zoomMenu
        items: ["100%", "125%", "150%", "200%", "400%", "800%"]
        showIconSlot: false
        onItemTriggered: function (index) {
            root.timelineScale = [1, 1.25, 1.5, 2, 4, 8][index];
        }
    }
    LV.ContextMenu {
        id: fpsMenu
        items: ["12 fps", "24 fps", "25 fps", "30 fps", "50 fps", "60 fps", "120 fps"]
        showIconSlot: false
        onItemTriggered: function (index) {
            root.stateModel.setFrameRate([12, 24, 25, 30, 50, 60, 120][index]);
        }
    }
    LV.ContextMenu {
        id: moreMenu
        items: [qsTr("Select all"), qsTr("Deselect all"), qsTr("Delete selected layers")]
        showIconSlot: false
        onItemTriggered: function (index) {
            if (index === 0)
                root.stateModel.selectAll();
            else if (index === 1)
                root.stateModel.clearSelection();
            else
                root.stateModel.removeSelected();
        }
    }
    LV.ContextMenu {
        id: trackMenu
        property string targetId: ""
        items: [qsTr("Rename"), qsTr("Move up"), qsTr("Move down"), qsTr("Duplicate"), qsTr("Delete")]
        showIconSlot: false
        onItemTriggered: function (index) {
            if (index === 0)
                root.renameTarget = targetId;
            else if (index < 3)
                root.stateModel.moveTrack(targetId, index === 1 ? -1 : 1);
            else if (index === 3) {
                root.stateModel.selectTrack(targetId);
                root.stateModel.duplicateSelected();
            } else
                root.stateModel.removeTrack(targetId);
        }
    }
    FileDialog {
        id: imageDialog
        objectName: "editorTimelineImageDialog"
        property string targetLayer: ""
        property var targetCanvas: null
        property int targetFrame: 0
        title: targetLayer.length ? qsTr("Choose frame content") : qsTr("Import canvas layer")
        nameFilters: [qsTr("Images and canvases (*.iisc *.png *.jpg *.jpeg *.webp *.heic *.avif)")]
        onAccepted: {
            if (targetCanvas !== root.canvas) {
                root.previewError(qsTr("The target canvas changed. Choose the frame again."));
                return;
            }
            const ok = targetLayer.length ? root.stateModel.importFrameSource(targetLayer, targetFrame, selectedFile) : root.canvas.placeImage(selectedFile);
            if (!ok)
                root.previewError(root.stateModel.error || root.canvas.error);
        }
    }
}
