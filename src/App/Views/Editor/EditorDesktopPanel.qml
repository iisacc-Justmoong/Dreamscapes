pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Rectangle {
    id: root
    property bool sharedBorders: false
    readonly property Item panelFrame: Frames.PanelFrame {
        parent: root; panelId: "editor-inspector"
        topEdge: !root.sharedBorders; rightEdge: !root.sharedBorders
        bottomEdge: !root.sharedBorders; leftEdge: !root.sharedBorders
    }
    objectName: "editorDesktopPanel"
    required property var toolState
    readonly property var layerPresenter: layerLoader.item
    readonly property bool layerModalActive: layerPresenter ? layerPresenter.modalActive : false
    readonly property bool backgroundModalActive: backgroundLoader.item ? backgroundLoader.item.modalActive : false
    readonly property bool graphicModalActive: graphicLoader.item ? graphicLoader.item.modalActive : false
    readonly property bool elementsModalActive: elementsLoader.item ? elementsLoader.item.modalActive : false
    readonly property bool photoModalActive: photoLoader.item ? photoLoader.item.modalActive : false
    signal closeRequested()
    signal actionRequested(string toolId, string fieldId, var values)
    color: "transparent"
    radius: 0
    readonly property real contentInset: LV.Theme.gap12
    border.width: 0
    border.color: "transparent"

    Loader {
        id: fileLoader
        anchors.fill: parent
        anchors.margins: root.contentInset
        active: root.toolState.toolId === "file"
        sourceComponent: EditorFilePanel { toolState: root.toolState }
    }

    Flickable {
        id: viewport
        visible: !fileLoader.active
        objectName: "editorDesktopPanelViewport"
        anchors.fill: parent
        anchors.margins: root.contentInset
        contentWidth: width
        contentHeight: backgroundLoader.active ? backgroundLoader.height : photoLoader.active ? photoLoader.height : elementsLoader.active ? elementsLoader.height : graphicLoader.active ? graphicLoader.height : textLoader.active ? textLoader.height : layerLoader.active ? layerLoader.height : compactLoader.active ? compactLoader.height : genericLoader.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        onContentHeightChanged: contentY = Math.max(0, Math.min(contentY, contentHeight - height))
        Connections {
            target: root.toolState
            function onToolIdChanged() { viewport.contentY = 0 }
        }
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        Loader {
            id: backgroundLoader
            width: viewport.width
            height: item ? item.implicitHeight : 0
            active: root.toolState.toolId === "background"
            sourceComponent: EditorBackgroundPanel { toolState: root.toolState }
        }
        Loader {
            id: photoLoader
            width: viewport.width
            height: item ? (item as Item).implicitHeight : 0
            active: root.toolState.toolId === "camera-photo"
            sourceComponent: EditorCameraPhotoPanel { toolState: root.toolState }
        }
        Loader {
            id: elementsLoader
            width: viewport.width
            height: item ? (item as Item).implicitHeight : 0
            active: root.toolState.toolId === "elements"
            sourceComponent: EditorElementsPanel { toolState: root.toolState }
        }
        Loader {
            id: graphicLoader
            width: viewport.width
            height: item ? (item as Item).implicitHeight : 0
            active: NativeData.presentation.indexOf(["color", "auto-enhance", "masking", "effects"], root.toolState.toolId) >= 0
            sourceComponent: EditorGraphicPanel { toolState: root.toolState }
        }
        Loader {
            id: textLoader
            width: viewport.width
            active: root.toolState.toolId === "text"
            height: item ? (item as Item).implicitHeight : 0
            sourceComponent: EditorTextPanel { toolState: root.toolState }
        }
        Loader {
            id: layerLoader
            width: viewport.width
            height: root.layerPresenter ? Math.max(viewport.height, root.layerPresenter.implicitHeight) : 0
            active: root.toolState.toolId === "layers" && Boolean(root.toolState.engine)
            function loadPanel() {
                if (active && !source.toString())
                    setSource(Qt.resolvedUrl("EditorLayersPanel.qml"), {toolState: root.toolState})
                else if (!active)
                    source = ""
            }
            onActiveChanged: loadPanel()
            Component.onCompleted: loadPanel()
        }
        Loader {
            id: compactLoader
            width: viewport.width
            height: item ? (item as Item).implicitHeight : 0
            active: Boolean(NativeData.compact.tool(root.toolState.toolId))
            sourceComponent: EditorCompactPanel { toolState: root.toolState }
        }
        Loader {
            id: genericLoader
            active: !compactLoader.active && !backgroundLoader.active && !fileLoader.active && !photoLoader.active && !elementsLoader.active && !graphicLoader.active && !layerLoader.active && !textLoader.active
            width: viewport.width
            height: item ? (item as Item).implicitHeight : 0
            sourceComponent: EditorToolPanel {
                engine: root.toolState.engine
                notice: root.toolState.notice
                definition: root.toolState.definition
                values: root.toolState.values
                sampleLayout: true
                onEdited: function(fieldId, value) {
                    if (genericLoader.active) root.toolState.setValue(fieldId, value)
                }
                onResetRequested: root.toolState.resetTool()
                onCloseRequested: root.closeRequested()
                onColorRequested: function(fieldId) { root.toolState.chooseColor(fieldId) }
                onActionRequested: function(fieldId, label) {
                    root.actionRequested(root.toolState.toolId, fieldId, root.toolState.values)
                }
            }
        }
    }
}
