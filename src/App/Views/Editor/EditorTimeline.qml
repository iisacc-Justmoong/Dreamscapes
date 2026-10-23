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
        id: timelineModel
        canvas: root.canvas
    }
    property alias playing: timelineModel.playing
    property alias timelineScale: timelineView.zoom
    property bool editingTime: false
    property string renameTarget: ""
    // Viewing a short document must not stretch its only frame across the ruler.
    readonly property EditorTimelineViewport viewState: EditorTimelineViewport {
        id: timelineView
        objectName: "editorTimelineViewState"
        frameCount: root.stateModel.totalFrames
        frameRate: root.stateModel.fps
        viewportWidth: lanes.width
        viewportHeight: viewport.height
        verticalOffset: viewport.contentY - lanes.y
        contentX: lanes.contentX
        trackLayout: root.stateModel.trackLayout
        dragPreview: root.stateModel.dragPreview
        keyDragPreview: root.stateModel.keyDragPreview
    }
    readonly property int viewFrameCount: timelineView.viewFrameCount
    readonly property int firstVisibleFrame: timelineView.firstVisibleFrame
    readonly property int lastVisibleFrame: timelineView.lastVisibleFrame
    readonly property int rulerHeight: 44
    readonly property color rulerTickColor: "#8d8d8d"
    readonly property color rulerBaselineColor: "#f0f000"
    readonly property color rulerPlayheadColor: "#4096f3"
    readonly property bool modalActive: imageDialog.visible
    readonly property int gutter: width < 600 ? 88 : width < 1100 ? 144 : 184
    readonly property bool wide: width >= 900
    readonly property var selected: stateModel.selectedClip
    readonly property var selectedKey: stateModel.selectedKey
    readonly property var trackLayout: stateModel.trackLayout
    readonly property real laneHeight: stateModel.laneHeight
    readonly property int tickStep: timelineView.majorStep
    readonly property int minorTickStep: timelineView.minorStep
    implicitHeight: footer.y + footer.height + 1
    signal previewError(string message)
    function frameLabel(frame) {
        return timelineView.frameLabel(frame);
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
    function zoomTimeline(factor, anchor) {
        lanes.contentX = timelineView.zoomAt(factor, anchor);
    }
    function rowSpacer(row, spacer) {
        const items = row.children.filter(item => item !== spacer && item.visible && item.width > 0);
        return root.wide ? Math.max(0, row.width - items.reduce((sum, item) => sum + item.width, 0) - row.spacing * items.length) : 0;
    }
    function chooseFrame(id) {
        imageDialog.targetCanvas = canvas;
        imageDialog.targetLayer = id;
        imageDialog.targetFrame = stateModel.playhead;
        imageDialog.open();
    }
    onVisibleChanged: if (!visible) {
        playing = false;
        pointerInput.cancelGesture();
        stateModel.cancelThumbnails();
    }
    onEnabledChanged: if (!enabled) {
        playing = false;
        pointerInput.cancelGesture();
        stateModel.cancelThumbnails();
    }
    onCanvasChanged: {
        playing = false;
        pointerInput.cancelGesture();
        Qt.callLater(fitTimeline);
    }
    Connections {
        target: root.canvas
        function onDocumentChanged() {
            root.playing = false;
            pointerInput.cancelGesture();
        }
    }
    Connections {
        target: root.stateModel
        function onPlaybackFailed(message) { root.previewError(message); }
    }
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape && (pointerInput.pressed || stateModel.dragging || stateModel.scrubbing)) {
            pointerInput.cancelGesture();
            event.accepted = true;
        } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal || event.key === Qt.Key_Minus) {
            zoomTimeline(event.key === Qt.Key_Minus ? 1 / 1.25 : 1.25, lanes.width / 2);
            event.accepted = true;
        } else if (event.key === Qt.Key_0) {
            fitTimeline();
            event.accepted = true;
        } else if (event.key === Qt.Key_Space) {
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
        interval: 16
        repeat: true
        running: pointerInput.pressed && !pointerInput.cancelled && root.stateModel.dragging && root.visible && root.enabled
        property double previous: 0
        onRunningChanged: previous = Date.now()
        onTriggered: {
            const now = Date.now();
            const next = timelineView.edgeScroll(pointerInput.viewportPointerX, now - previous);
            previous = now;
            if (next !== lanes.contentX) {
                lanes.contentX = next;
                root.stateModel.updateDrag(lanes.contentX + pointerInput.viewportPointerX);
            }
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
        LV.Tooltip {
            target: button
            text: button.Accessible.name
        }
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
                        text: root.stateModel.preparing ? qsTr("Preparing audio…") : root.stateModel.timecode
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
                    Accessible.name: root.stateModel.preparing ? qsTr("Cancel audio preparation") : root.playing ? qsTr("Pause timeline") : qsTr("Play timeline")
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
                        text: modelData + " " + (modelData === "All" ? root.stateModel.layerCount : modelData === "Kept" ? root.stateModel.keptCount : root.stateModel.layerCount - root.stateModel.keptCount)
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
                model: timelineView.visibleTracks
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
                contentWidth: timelineView.contentWidth
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
                        model: timelineView.majorTicks
                        delegate: Item {
                            required property var modelData
                            objectName: "editorTimelineRulerTick-" + modelData.frame
                            x: modelData.x
                            width: 1
                            height: laneContent.height
                            Rectangle {
                                objectName: "editorTimelineRulerMark-" + parent.modelData.frame
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
                                objectName: "editorTimelineRulerLabel-" + parent.modelData.frame
                                x: 2
                                y: 1
                                text: root.frameLabel(parent.modelData.frame)
                                style: caption
                                color: LV.Theme.textSecondary
                            }
                        }
                    }
                    Repeater {
                        model: timelineView.minorTicks
                        delegate: Rectangle {
                            required property var modelData
                            readonly property int frame: modelData.frame
                            objectName: "editorTimelineRulerMinor-" + frame
                            x: modelData.x
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
                        model: timelineView.visibleTracks
                        delegate: Item {
                            id: trackRow
                            required property var modelData
                            objectName: "editorTimelineRow-" + modelData.id
                            y: modelData.top
                            width: laneContent.width
                            height: modelData.height
                            readonly property bool inViewport: lanes.y + y + height >= viewport.contentY && lanes.y + y <= viewport.contentY + viewport.height
                            Repeater {
                                model: trackRow.modelData.clips
                                delegate: Rectangle {
                                    id: card
                                    required property var modelData
                                    readonly property var displayData: root.stateModel.dragPreview[modelData.id] || modelData
                                    readonly property real visibleLeft: Math.max(0, lanes.contentX - x)
                                    readonly property real visibleWidth: Math.max(0, Math.min(width - visibleLeft, lanes.width))
                                    objectName: "editorTimelineClip-" + modelData.id
                                    x: displayData.in / root.viewFrameCount * trackRow.width
                                    y: 4
                                    width: Math.max(1, (displayData.out - displayData.in) / root.viewFrameCount * trackRow.width - 4)
                                    height: modelData.type === "Audio" ? 40 : 32
                                    radius: 8
                                    clip: true
                                    color: LV.Theme.surfaceControl
                                    opacity: modelData.excluded || displayData.valid === false ? 0.4 : 1
                                    border.width: root.stateModel.selectedIds.indexOf(modelData.id) >= 0 ? 1 : 0
                                    border.color: LV.Theme.accent
                                    Image {
                                        id: preview
                                        property int completion: 0
                                        Connections {
                                            target: root.stateModel
                                            function onThumbnailReady(layerId) {
                                                if (layerId === card.modelData.id)
                                                    preview.completion++;
                                            }
                                        }
                                        objectName: "editorTimelineThumbnail-" + card.modelData.id
                                        x: card.visibleLeft + 4
                                        y: 2
                                        width: 32
                                        height: 28
                                        visible: card.modelData.type === "Layer" && card.visibleWidth > 44
                                        source: {
                                            const revision = root.stateModel.thumbnailRevision;
                                            const completion = preview.completion;
                                            return visible && trackRow.inViewport && root.visible && root.enabled ? root.stateModel.requestThumbnail(card.modelData.id, root.stateModel.playhead) : "";
                                        }
                                        fillMode: Image.PreserveAspectCrop
                                    }
                                    LV.Label {
                                        x: card.visibleLeft + (card.modelData.type === "Layer" && card.visibleWidth > 44 ? 44 : 4)
                                        y: 2
                                        width: Math.max(0, card.visibleLeft + card.visibleWidth - x - 4)
                                        text: card.modelData.name
                                        style: body
                                        elide: Text.ElideRight
                                    }
                                    LV.Label {
                                        x: card.visibleLeft + (card.visibleWidth > 44 ? 44 : 4)
                                        y: 17
                                        width: Math.max(0, card.visibleLeft + card.visibleWidth - x - 4)
                                        visible: card.modelData.type === "Layer"
                                        text: root.frameLabel(card.displayData.in) + " · " + card.modelData.timing + " · " + card.displayData.frames + "f"
                                        style: caption
                                        color: LV.Theme.textSecondary
                                        elide: Text.ElideRight
                                    }
                                    Canvas {
                                        id: wave
                                        objectName: "editorTimelineWaveform-" + card.modelData.id
                                        x: Math.max(2, card.visibleLeft)
                                        y: 15
                                        width: Math.max(0, Math.min(card.width - x - 2, lanes.width))
                                        height: 23
                                        visible: card.modelData.type === "Audio"
                                        onWidthChanged: requestPaint()
                                        onXChanged: requestPaint()
                                        onPaint: {
                                            const values = card.modelData.waveform || [], c = getContext("2d");
                                            c.clearRect(0, 0, width, height);
                                            c.strokeStyle = LV.Theme.accentGreen;
                                            c.lineWidth = 1;
                                            c.beginPath();
                                            values.forEach((value, index) => {
                                                const x = 2 + index / Math.max(1, values.length - 1) * (card.width - 4) - wave.x, peak = value * height / 2;
                                                c.moveTo(x, height / 2 - peak);
                                                c.lineTo(x, height / 2 + peak);
                                            });
                                            c.stroke();
                                        }
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
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: trackRow.modelData.keys
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property var displayData: root.stateModel.keyDragPreview.id === modelData.id ? root.stateModel.keyDragPreview : modelData
                                    objectName: "editorTimelineKey-" + modelData.id
                                    x: displayData.frame / root.viewFrameCount * trackRow.width - 3
                                    y: 1
                                    width: 6
                                    height: 6
                                    rotation: 45
                                    color: displayData.valid === false ? LV.Theme.accentRed : root.selectedKey.id === modelData.id ? LV.Theme.accent : LV.Theme.titleHeaderColor
                                }
                            }
                            Canvas {
                                id: curve
                                objectName: "editorTimelineCurve-" + trackRow.modelData.id
                                x: lanes.contentX
                                y: 40
                                width: Math.max(0, Math.min(lanes.width, trackRow.width - x))
                                height: 64
                                visible: trackRow.modelData.curve
                                Connections {
                                    target: root.stateModel
                                    function onLayoutChanged() {
                                        curve.requestPaint();
                                    }
                                    function onDragPreviewChanged() {
                                        if (curve.visible)
                                            curve.requestPaint();
                                    }
                                }
                                onWidthChanged: requestPaint()
                                onXChanged: requestPaint()
                                onPaint: {
                                    const c = getContext("2d");
                                    c.clearRect(0, 0, width, height);
                                    c.strokeStyle = LV.Theme.accent;
                                    c.lineWidth = 2;
                                    c.beginPath();
                                    const keys = timelineView.curveKeys(trackRow.modelData.id);
                                    keys.forEach((key, index) => {
                                        const x = key.frame / root.viewFrameCount * trackRow.width - curve.x, y = (1 - key.value) * height;
                                        if (!index)
                                            c.moveTo(x, y);
                                        else if (keys[index - 1].interpolation === "Hold") {
                                            c.lineTo(x, (1 - keys[index - 1].value) * height);
                                            c.lineTo(x, y);
                                        } else if (keys[index - 1].interpolation === "Smooth") {
                                            const previous = keys[index - 1], px = previous.frame / root.viewFrameCount * trackRow.width - curve.x, py = (1 - previous.value) * height;
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
                    // The pointer owner survives culling of any row or clip delegate.
                    MouseArea {
                        id: pointerInput
                        objectName: "editorTimelinePointerInput"
                        anchors.fill: parent
                        z: 6
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        hoverEnabled: true
                        preventStealing: true
                        property var pressedHit: ({})
                        property string pressedTool: ""
                        property bool cancelled: false
                        property real pressX: 0
                        property real viewportPointerX: 0
                        property real panStartX: 0
                        property real panStartScroll: 0
                        property real panStartY: 0
                        property real panStartScrollY: 0
                        readonly property var hoverHit: timelineView.hitTest(mouseX, mouseY)
                        cursorShape: pressedHit.kind === "pan" ? Qt.ClosedHandCursor : root.stateModel.tool === "Blade" ? Qt.CrossCursor : hoverHit.edge ? Qt.SizeHorCursor : Qt.ArrowCursor
                        function cancelGesture() {
                            cancelled = true;
                            pressedHit = ({});
                            root.stateModel.cancelDrag();
                            root.stateModel.cancelScrub();
                        }
                        onPressed: function (mouse) {
                            root.forceActiveFocus();
                            cancelled = false;
                            pressedTool = root.stateModel.tool;
                            pressX = mouse.x;
                            viewportPointerX = mouse.x - lanes.contentX;
                            if (mouse.button === Qt.MiddleButton) {
                                panStartX = viewportPointerX;
                                panStartScroll = lanes.contentX;
                                pressedHit = {kind: "pan"};
                                return;
                            }
                            // Capture the target/edge before any preview moves it.
                            pressedHit = timelineView.hitTest(mouse.x, mouse.y);
                            if (pressedHit.kind === "ruler") {
                                if (mouse.button === Qt.LeftButton)
                                    root.stateModel.beginScrub(timelineView.frameAt(mouse.x));
                                else
                                    pressedHit = ({});
                            } else if (mouse.button === Qt.LeftButton && root.stateModel.tool === "Range") {
                                pressedHit = {kind: "range", frame: timelineView.frameAt(mouse.x)};
                                root.stateModel.markRange(pressedHit.frame, pressedHit.frame);
                            } else if (pressedHit.kind === "key") {
                                root.stateModel.selectKey(pressedHit.id);
                                if (mouse.button === Qt.LeftButton)
                                    root.stateModel.beginKeyDrag(pressedHit.id, mouse.x, timelineView.pixelsPerFrame, timelineView.snapTolerance);
                            } else if (pressedHit.kind === "clip") {
                                const multiple = Boolean(mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier));
                                if (multiple || root.stateModel.selectedIds.indexOf(pressedHit.id) < 0 || root.selectedKey.id)
                                    root.stateModel.selectClip(pressedHit.id, multiple);
                                if (mouse.button === Qt.RightButton) {
                                    trackMenu.targetId = pressedHit.trackId;
                                    trackMenu.openFor(pointerInput, mouse.x, mouse.y);
                                } else if (!pressedHit.locked && root.stateModel.tool !== "Blade" && root.stateModel.selectedIds.indexOf(pressedHit.id) >= 0) {
                                    root.stateModel.beginDrag(pressedHit.id, pressedHit.edge, mouse.x, timelineView.pixelsPerFrame, timelineView.snapTolerance);
                                }
                            } else {
                                if (!(mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)))
                                    root.stateModel.clearSelection();
                                // Nested horizontal Flickables cannot reliably forward a
                                // mouse grab vertically. Pan the dock without editing a clip.
                                panStartY = mapToItem(viewport, 0, mouse.y).y;
                                panStartScrollY = viewport.contentY;
                                pressedHit = {kind: "viewPan"};
                            }
                        }
                        onPositionChanged: function (mouse) {
                            viewportPointerX = mouse.x - lanes.contentX;
                            if (!pressed || cancelled)
                                return;
                            if (pressedHit.kind === "pan") {
                                lanes.contentX = timelineView.boundedScroll(panStartScroll + panStartX - viewportPointerX);
                            } else if (pressedHit.kind === "viewPan") {
                                viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, panStartScrollY + panStartY - mapToItem(viewport, 0, mouse.y).y));
                            } else if (pressedHit.kind === "ruler") {
                                root.stateModel.updateScrub(timelineView.frameAt(mouse.x));
                            } else if (pressedHit.kind === "range") {
                                root.stateModel.markRange(pressedHit.frame, timelineView.frameAt(mouse.x));
                            } else if (root.stateModel.dragging) {
                                root.stateModel.updateDrag(mouse.x);
                            }
                        }
                        onCanceled: cancelGesture()
                        onReleased: function (mouse) {
                            if (!cancelled && mouse.button === Qt.LeftButton) {
                                if (pressedHit.kind === "ruler") {
                                    root.stateModel.finishScrub(timelineView.frameAt(mouse.x));
                                } else if (pressedHit.kind === "range") {
                                    root.stateModel.selectMarkedRange();
                                } else if (pressedHit.kind === "clip" && !pressedHit.locked && pressedTool === "Blade" && root.stateModel.tool === pressedTool) {
                                    if (root.stateModel.selectClip(pressedHit.id)) {
                                        root.seekFrame(timelineView.frameAt(pressX));
                                        root.stateModel.split();
                                    }
                                } else if (root.stateModel.dragging) {
                                    root.stateModel.updateDrag(mouse.x);
                                    root.stateModel.finishDrag();
                                }
                            }
                            pressedHit = ({});
                        }
                        onDoubleClicked: if (!cancelled && pressedHit.type === "Layer" && pressedHit.timing === "Dynamic" && pressedHit.representation === "Bitmap")
                            root.chooseFrame(pressedHit.id)
                        onWheel: function (wheel) {
                            if (root.stateModel.dragging || pressed) {
                                wheel.accepted = false;
                            } else if (wheel.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
                                root.zoomTimeline(Math.pow(1.25, wheel.angleDelta.y / 120), wheel.x - lanes.contentX);
                                wheel.accepted = true;
                            } else if ((wheel.modifiers & Qt.ShiftModifier) || wheel.pixelDelta.x || wheel.angleDelta.x) {
                                const delta = wheel.pixelDelta.x || wheel.pixelDelta.y || wheel.angleDelta.x / 2 || wheel.angleDelta.y / 2;
                                lanes.contentX = timelineView.boundedScroll(lanes.contentX - delta);
                                wheel.accepted = true;
                            } else {
                                wheel.accepted = false;
                            }
                        }
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
                    text: qsTr("%1 layers · %2 kept · %3f · %4 keys").arg(root.stateModel.layerCount).arg(root.stateModel.keptCount).arg(root.stateModel.totalFrames).arg(root.stateModel.keys.length)
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
            root.zoomTimeline([1, 1.25, 1.5, 2, 4, 8][index] / root.timelineScale, lanes.width / 2);
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
