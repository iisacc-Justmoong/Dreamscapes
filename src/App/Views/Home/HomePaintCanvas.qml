pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName: "homePaintCanvas"
    property string aspectRatio: "1:1"
    property alias canvas: canvas
    readonly property bool hasInputs: canvas.hasContent || canvas.attachments.length > 0
    property Item attachmentHost: null
    readonly property real attachmentsHeight: canvas.attachments.length > 0 ? 64 : 0
    property var draggedCard: null
    property bool dragging: false
    implicitHeight: layout.implicitHeight
    onAspectRatioChanged: canvas.setAspectRatio(aspectRatio)
    function addAttachment(source) { return canvas.addAttachment(source) }
    function generationParameters(prompt, model, ratio, count) {
        return canvas.generationParameters(prompt, model, ratio, count)
    }
    function openPopover(popover, button) {
        const p = button.mapToItem(popover.parent, 0, button.height + 4)
        popover.x = Math.max(8, Math.min(p.x, popover.parent.width - popover.width - 8))
        popover.y = Math.max(8, Math.min(p.y, popover.parent.height - popover.height - 8))
        popover.open()
    }
    function dismissPopovers() { colorPopover.close() }
    component Tool: LV.IconButton {
        property string help: ""
        iconSize: 18
        tone: LV.AbstractButton.Default
        implicitWidth: 22
        implicitHeight: 22
        Accessible.name: help
        Controls.ToolTip.visible: hovered
        Controls.ToolTip.text: help
        Controls.ToolTip.delay: 500
    }
    ColumnLayout {
        id: layout
        width: parent.width
        spacing: 8
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: panel.implicitHeight + 24
            color: LV.Theme.panelBackground08
            radius: 12
            ColumnLayout {
                id: panel
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 8
                Flow {
                    id: toolbar
                    objectName: "homeCanvasToolbar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 8
                    Tool {
                        objectName: "homeBrushButton"
                        iconSource: Qt.resolvedUrl("Assets/Paint/brush.svg")
                        help: qsTr("Brush")
                        tone: canvas.toolMode === "brush" ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                        onClicked: canvas.toolMode = "brush"
                    }
                    Tool {
                        objectName: "homeEraserButton"
                        iconName: "eraser"
                        help: qsTr("Eraser")
                        tone: canvas.toolMode === "eraser" ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                        onClicked: canvas.toolMode = "eraser"
                    }
                    Row {
                        width: 186; height: 22; spacing: 8
                        Image {
                            objectName: "homeBrushSizeIcon"
                            width: 18; height: 18; y: 2
                            source: LV.Theme.iconPath("brush-size")
                        }
                        LV.Slider {
                            objectName: "homeBrushSizeSlider"
                            width: 120; height: 22
                            from: 1; to: 500; stepSize: 1
                            value: canvas.brushSize
                            size: LV.Slider.Mini
                            showSymbol: false; showLabels: false
                            Accessible.name: qsTr("Brush size in pixels")
                            onMoved: canvas.brushSize = value
                        }
                        LV.Label {
                            width: 32; height: 22
                            text: Math.round(canvas.brushSize) + qsTr("px")
                            style: description
                            color: LV.Theme.descriptionColor
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Row {
                        width: 188; height: 22; spacing: 8
                        Image {
                            objectName: "homeOpacityIcon"
                            width: 18; height: 18; y: 2
                            source: LV.Theme.iconPath("brush-opacity")
                        }
                        LV.Slider {
                            objectName: "homeOpacitySlider"
                            width: 120; height: 22
                            from: 0; to: 100; stepSize: 1
                            value: canvas.brushOpacity * 100
                            size: LV.Slider.Mini
                            showSymbol: false; showLabels: false
                            Accessible.name: qsTr("Brush opacity in percent")
                            onMoved: canvas.brushOpacity = value / 100
                        }
                        LV.Label {
                            width: 34; height: 22
                            text: Math.round(canvas.brushOpacity * 100) + "%"
                            style: description
                            color: LV.Theme.descriptionColor
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    LV.ColorPickerButton {
                        id: colorButton
                        objectName: "homeColorButton"
                        buttonSize: LV.ColorPickerButton.Small
                        currentColor: canvas.brushColor
                        onClicked: {
                            colorPicker.setColor(canvas.brushColor)
                            colorPicker.previousColor = canvas.brushColor
                            root.openPopover(colorPopover, colorButton)
                        }
                    }
                    Item { width: Math.max(0, toolbar.width - 186 - 188 - 6 * 22 - 8 * 8); height: 22; visible: width > 0 }
                    Tool {
                        objectName: "homeUndoButton"
                        iconName: "generalundo"
                        help: qsTr("Undo")
                        enabled: canvas.canUndo
                        onClicked: canvas.undo()
                    }
                    Tool {
                        objectName: "homeRedoButton"
                        iconName: "generalredo"
                        help: qsTr("Redo")
                        enabled: canvas.canRedo
                        onClicked: canvas.redo()
                    }
                    Tool {
                        objectName: "homeClearButton"
                        iconName: "generalremove"
                        help: qsTr("Clear canvas")
                        enabled: canvas.hasContent
                        onClicked: canvas.clearSelectedLayer()
                    }
                }
                Flickable {
                    id: canvasViewport
                    objectName: "homeCanvasViewport"
                    Layout.fillWidth: true
                    implicitHeight: stage.height
                    contentWidth: Math.max(width, stage.width)
                    contentHeight: stage.height
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: !canvas.liveStrokeActive
                    clip: true
                    Controls.ScrollBar.horizontal: Controls.ScrollBar {
                        objectName: "homeCanvasScrollBar"
                        policy: Controls.ScrollBar.AsNeeded
                        visible: canvasViewport.contentWidth > canvasViewport.width
                    }
                    Rectangle {
                        id: stage
                        objectName: "homeCanvasStage"
                        width: Math.max(0, Math.min(1080, canvasViewport.width))
                        height: canvas.height
                        x: Math.max(0, (canvasViewport.width - width) / 2)
                        color: "#f3f1e8"
                        HomeCanvas {
                            id: canvas
                            objectName: "homeCanvasPixels"
                            width: stage.width
                            height: width * canvasHeight / canvasWidth
                            clip: true
                            onWidthChanged: Qt.callLater(fitToView)
                            onHeightChanged: Qt.callLater(fitToView)
                            Component.onCompleted: { setAspectRatio(root.aspectRatio); Qt.callLater(fitToView) }
                            DropArea {
                                id: dropZone
                                objectName: "homeCanvasDropZone"
                                anchors.fill: parent
                                keys: ["dreamscapes/home-image"]
                                onDropped: function(drop) {
                                    if (drop.source && canvas.pasteAttachment(drop.source.imageSource, drop.x, drop.y))
                                        drop.acceptProposedAction()
                                }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: dropZone.containsDrag ? LV.Theme.primary : "#d9d9cc"
                            border.width: dropZone.containsDrag ? 2 : 1
                        }
                    }
                    LV.Label {
                        parent: canvasViewport
                        anchors.centerIn: parent
                        width: Math.max(0, Math.min(stage.width, canvasViewport.width) - 24)
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        sizeToContentHeight: true
                        visible: !canvas.hasContent && !dropZone.containsDrag
                        text: qsTr("Paint here or drag an attached image")
                        color: "#85867b"
                        style: description
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    LV.Label { text: root.aspectRatio + " · " + canvas.canvasWidth + " × " + canvas.canvasHeight; style: description; color: LV.Theme.descriptionColor }
                }
            }
        }
        ColumnLayout {
            parent: root.attachmentHost || layout
            width: parent.width
            visible: canvas.attachments.length > 0
            Layout.fillWidth: true
            spacing: 0
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                contentWidth: cards.implicitWidth
                contentHeight: 64
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                interactive: !root.dragging
                Row {
                    id: cards
                    spacing: 8
                    Repeater {
                        model: canvas.attachments
                        delegate: Rectangle {
                            id: card
                            required property var modelData
                            required property int index
                            property url imageSource: modelData.source
                            objectName: "homeAttachment_" + index
                            width: 340
                            height: 64
                            radius: 8
                            color: LV.Theme.panelBackground08
                            Image {
                                x: 8; y: 8; width: 48; height: 48
                                source: card.imageSource
                                sourceSize: Qt.size(96, 96)
                                fillMode: Image.PreserveAspectCrop
                                clip: true
                            }
                            LV.VStack {
                                x: 64; y: 17; width: 190; spacing: 4
                                LV.Label { Layout.fillWidth: true; text: card.modelData.filename; style: body; elide: Text.ElideRight }
                                LV.Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Image · %1").arg(card.modelData.sizeBytes >= 1048576
                                        ? (card.modelData.sizeBytes / 1048576).toFixed(1) + " MB"
                                        : Math.max(1, Math.round(card.modelData.sizeBytes / 1024)) + " KB")
                                    style: description; font.pixelSize: 11; color: LV.Theme.descriptionColor
                                }
                            }
                            MouseArea {
                                id: dragMouse
                                objectName: "homeAttachmentDrag_" + card.index
                                anchors.fill: parent
                                enabled: root.visible
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                preventStealing: true
                                property point pressPosition
                                onPressed: function(mouse) {
                                    root.draggedCard = card
                                    pressPosition = Qt.point(mouse.x, mouse.y)
                                    const p = dragMouse.mapToItem(root, mouse.x, mouse.y)
                                    dragPreview.x = p.x - 24
                                    dragPreview.y = p.y - 24
                                }
                                onPositionChanged: function(mouse) {
                                    if (!pressed) return
                                    const p = dragMouse.mapToItem(root, mouse.x, mouse.y)
                                    dragPreview.x = p.x - 24
                                    dragPreview.y = p.y - 24
                                    if (Math.abs(mouse.x - pressPosition.x) + Math.abs(mouse.y - pressPosition.y) > 6)
                                        root.dragging = true
                                }
                                onReleased: {
                                    if (root.dragging) dragPreview.Drag.drop()
                                    root.dragging = false
                                    root.draggedCard = null
                                }
                                onCanceled: { root.dragging = false; root.draggedCard = null }
                            }
                            LV.LabelButton {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Remove")
                                tone: LV.AbstractButton.Borderless
                                Accessible.name: qsTr("Remove %1").arg(card.modelData.filename)
                                onClicked: canvas.removeAttachment(card.index)
                            }
                        }
                    }
                }
            }
        }
        LV.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: canvas.inputError
            color: LV.Theme.descriptionColor
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
    }
    Image {
        id: dragPreview
        objectName: "homeAttachmentDragPreview"
        z: 100
        width: 72
        height: 48
        visible: root.dragging
        opacity: 0.85
        source: root.draggedCard ? root.draggedCard.imageSource : ""
        sourceSize: Qt.size(144, 96)
        fillMode: Image.PreserveAspectFit
        Drag.active: root.dragging
        Drag.source: root.draggedCard
        Drag.hotSpot.x: 24
        Drag.hotSpot.y: 24
        Drag.keys: ["dreamscapes/home-image"]
        Drag.supportedActions: Qt.CopyAction
        Drag.proposedAction: Qt.CopyAction
    }
    LV.Popover {
        id: colorPopover
        objectName: "homeColorPopover"
        width: colorPicker.implicitWidth + padding * 2
        height: colorPicker.implicitHeight + padding * 2
        contentItem: LV.ColorPicker {
            id: colorPicker
            objectName: "homeColorPicker"
            onAccepted: function(color) { canvas.brushColor = color; colorPopover.close() }
            onCanceled: colorPopover.close()
        }
    }
}
