pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    required property var canvas
    required property var project
    property bool mobileLayout: false
    property bool showCommandBar: true
    readonly property bool modalActive: commandMenu.visible || openDialog.visible || placeDialog.visible || saveDialog.visible || layersSheet.visible || paintColorSheet.visible
    signal openDocumentRequested(url source, bool asCopy)
    implicitHeight: navigation.y + navigation.height + (errorLabel.visible ? errorLabel.implicitHeight + 8 : 0)
    function requestOpen(asCopy, directory) { openDialog.openAsCopy = Boolean(asCopy); if (directory && directory.toString().length) openDialog.currentFolder = directory; openDialog.open() }
    function requestPlace(placement) { placeDialog.placement = placement || "Fit"; placeDialog.open() }
    function requestSave() {
        if (project.filePath.length > 0) project.saveDocument()
        else saveDialog.open()
    }
    function requestSaveAs(name, directory) {
        if (directory && directory.toString().length) saveDialog.currentFolder = directory
        if (name && name.length) {
            const suffix = root.project.isProject ? "iiscp" : "iisc"
            const folder = saveDialog.currentFolder.toString().replace(/\/$/, "")
            saveDialog.selectedFile = folder + "/" + encodeURIComponent(name) + "." + suffix
        }
        saveDialog.open()
    }
    function showLayers() { layersSheet.open() }
    function openContextMenu(target, x, y) { commandMenu.openFor(target, x, y) }
    function showPaintColor() {
        paintColorSheet.open()
        if (paintColorSheet.loadedContent) paintColorSheet.loadedContent.setColor(canvas.brushColor)
    }
    Flow {
        id: buttons
        width: parent.width
        visible: root.showCommandBar
        height: visible ? implicitHeight : 0
        spacing: 8
        LV.Label {
            text: (root.project.modified ? "* " : "") + root.project.documentName
            style: description
            width: Math.min(160, Math.max(72, root.width / 3))
            height: 22
            elide: Text.ElideRight
        }
        LV.LabelButton { objectName: "editorDocumentOpen"; text: qsTr("Open"); height: 22; onClicked: root.requestOpen() }
        LV.LabelButton { objectName: "editorDocumentSave"; text: qsTr("Save"); height: 22; onClicked: root.requestSave() }
        LV.LabelButton { objectName: "editorDocumentUndo"; text: qsTr("Undo"); height: 22; enabled: root.canvas.canUndo; onClicked: root.canvas.undo() }
        LV.LabelButton { objectName: "editorDocumentRedo"; text: qsTr("Redo"); height: 22; enabled: root.canvas.canRedo; onClicked: root.canvas.redo() }
        LV.LabelButton { objectName: "editorDocumentLayers"; text: qsTr("Layers"); height: 22; onClicked: root.showLayers() }
        LV.LabelButton { objectName: "editorDocumentFit"; text: qsTr("Fit"); height: 22; onClicked: root.canvas.fitToView() }
        LV.ColorPickerButton {
            objectName: "editorPaintColor"
            buttonSize: 22
            currentColor: root.canvas.brushColor
            onClicked: root.showPaintColor()
        }
        LV.Label { text: Math.round(root.canvas.zoom * 100) + "%"; style: description; height: 22 }
    }
    LV.HStack {
        id: navigation
        objectName: "editorCanvasNavigation"
        anchors.top: buttons.bottom
        anchors.topMargin: visible && buttons.visible ? 8 : 0
        height: visible ? 22 : 0
        visible: root.project.canvasCount > 1
        spacing: 8
        LV.LabelButton { objectName: "editorPreviousCanvas"; text: qsTr("Previous"); height: 22; enabled: root.project.currentIndex > 0; onClicked: root.project.setCurrentIndex(root.project.currentIndex - 1) }
        LV.Label { objectName: "editorCanvasPosition"; text: qsTr("Canvas %1 / %2").arg(root.project.currentIndex + 1).arg(root.project.canvasCount); height: 22; style: description }
        LV.LabelButton { objectName: "editorNextCanvas"; text: qsTr("Next"); height: 22; enabled: root.project.currentIndex + 1 < root.project.canvasCount; onClicked: root.project.setCurrentIndex(root.project.currentIndex + 1) }
    }
    LV.Label {
        id: errorLabel
        objectName: "editorDocumentError"
        anchors.top: navigation.bottom
        anchors.topMargin: 8
        width: parent.width
        visible: root.project.error.length > 0
        text: root.project.error
        style: description
        wrapMode: Text.Wrap
        sizeToContentHeight: true
    }
    LV.ContextMenu {
        id: commandMenu
        objectName: "editorDocumentContextMenu"
        showIconSlot: false
        items: [
            { label: qsTr("Open") },
            { label: qsTr("Save") },
            { label: qsTr("Save As") },
            { label: qsTr("Undo"), enabled: root.canvas.canUndo },
            { label: qsTr("Redo"), enabled: root.canvas.canRedo },
            { label: qsTr("Layers") },
            { label: qsTr("Fit") },
            { label: qsTr("Paint Color") }
        ]
        onItemTriggered: function(index) {
            if (index === 0) root.requestOpen()
            else if (index === 1) root.requestSave()
            else if (index === 2) root.requestSaveAs()
            else if (index === 3) root.canvas.undo()
            else if (index === 4) root.canvas.redo()
            else if (index === 5) root.showLayers()
            else if (index === 6) root.canvas.fitToView()
            else if (index === 7) root.showPaintColor()
        }
    }
    FileDialog {
        id: openDialog
        objectName: "editorDocumentOpenDialog"
        title: qsTr("Open Canvas")
        property bool openAsCopy: false
        nameFilters: [qsTr("Projects, canvases and images (*.iiscp *.iisc *.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)")]
        onAccepted: root.openDocumentRequested(selectedFile, openAsCopy)
        onVisibleChanged: if (!visible) Qt.callLater(function() { root.canvas.forceActiveFocus() })
    }
    FileDialog {
        id: placeDialog
        objectName: "editorDocumentPlaceDialog"
        title: qsTr("Place Image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)")]
        property string placement: "Fit"
        onAccepted: root.canvas.placeImage(selectedFile, placement)
        onVisibleChanged: if (!visible) Qt.callLater(function() { root.canvas.forceActiveFocus() })
    }
    FileDialog {
        id: saveDialog
        objectName: "editorDocumentSaveDialog"
        title: root.project.isProject ? qsTr("Save Project As") : qsTr("Save Canvas As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.project.isProject ? "iiscp" : "iisc"
        nameFilters: root.project.isProject ? [qsTr("Dreamscapes project (*.iiscp)")] : [qsTr("iiSharedCanvas (*.iisc)"), qsTr("Dreamscapes project (*.iiscp)")]
        onAccepted: root.project.saveDocumentAs(selectedFile)
        onVisibleChanged: if (!visible) Qt.callLater(function() { root.canvas.forceActiveFocus() })
    }
    LV.Sheet {
        id: paintColorSheet
        objectName: "editorPaintColorSheet"
        parent: root.parent.parent
        title: qsTr("Paint Color")
        presentation: root.mobileLayout ? LV.Sheet.Mobile : LV.Sheet.Desktop
        contentPadding: 16
        contentComponent: LV.ColorPicker {
            objectName: "editorPaintColorPicker"
            Component.onCompleted: setColor(root.canvas.brushColor)
            onAccepted: function(color) { root.canvas.brushColor = color; paintColorSheet.close() }
            onCanceled: paintColorSheet.close()
        }
    }
    LV.Sheet {
        id: layersSheet
        objectName: "editorDocumentLayersSheet"
        parent: root.parent.parent
        title: qsTr("Document Layers")
        presentation: root.mobileLayout ? LV.Sheet.Mobile : LV.Sheet.Desktop
        contentPadding: 16
        contentComponent: Column {
            spacing: 8
            Repeater {
                model: root.canvas.layers
                LV.HStack {
                    required property var modelData
                    id: layerRow
                    width: parent.width
                    spacing: 8
                    LV.LabelButton {
                        objectName: "editorLayerSelect-" + layerRow.modelData.id
                        text: layerRow.modelData.name + " · " + layerRow.modelData.type
                        tone: layerRow.modelData.selected ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                        Layout.fillWidth: true
                        Accessible.checkable: true
                        Accessible.checked: layerRow.modelData.selected
                        onClicked: root.canvas.selectLayer(layerRow.modelData.id)
                    }
                    LV.ToggleSwitch {
                        objectName: "editorLayerVisible-" + layerRow.modelData.id
                        checked: layerRow.modelData.visible
                        trackWidth: 38
                        trackHeight: 22
                        implicitHeight: 22
                        Accessible.name: qsTr("Show %1").arg(layerRow.modelData.name)
                        onToggled: root.canvas.setLayerVisible(layerRow.modelData.id, checked)
                    }
                }
            }
            LV.InputField {
                objectName: "editorLayerName"
                width: parent.width
                visible: Boolean(root.canvas.selectedLayer.id)
                text: root.canvas.selectedLayer.name || ""
                clearButtonVisible: false
                Accessible.name: qsTr("Layer name")
                onAccepted: function(value) { root.canvas.setLayerName(root.canvas.selectedLayer.id, value) }
            }
            LV.Slider {
                objectName: "editorLayerOpacity"
                width: parent.width
                visible: Boolean(root.canvas.selectedLayer.id)
                from: 0
                to: 100
                stepSize: 1
                value: (root.canvas.selectedLayer.opacity || 0) * 100
                Accessible.name: qsTr("Layer opacity")
                onMoved: root.canvas.setLayerOpacity(root.canvas.selectedLayer.id, value / 100)
            }
            LV.HStack {
                width: parent.width
                spacing: 8
                LV.InputField {
                    objectName: "editorLayerX"
                    Layout.fillWidth: true
                    text: String(root.canvas.selectedLayer.x || 0)
                    clearButtonVisible: false
                    Accessible.name: qsTr("Layer X")
                    onAccepted: function(value) { if (isFinite(Number(value))) root.canvas.setLayerTransform(root.canvas.selectedLayer.id, {x: Number(value)}) }
                }
                LV.InputField {
                    objectName: "editorLayerY"
                    Layout.fillWidth: true
                    text: String(root.canvas.selectedLayer.y || 0)
                    clearButtonVisible: false
                    Accessible.name: qsTr("Layer Y")
                    onAccepted: function(value) { if (isFinite(Number(value))) root.canvas.setLayerTransform(root.canvas.selectedLayer.id, {y: Number(value)}) }
                }
            }
            LV.HStack {
                spacing: 8
                LV.LabelButton { objectName: "editorAddPaintLayer"; text: qsTr("Add Paint Layer"); onClicked: root.canvas.addPaintLayer() }
                LV.LabelButton { objectName: "editorRemoveLayer"; text: qsTr("Delete Layer"); enabled: Boolean(root.canvas.selectedLayer.id); onClicked: root.canvas.removeLayer(root.canvas.selectedLayer.id) }
            }
        }
    }
}
