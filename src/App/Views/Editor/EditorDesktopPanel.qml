pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Rectangle {
    id: root
    objectName: "editorDesktopPanel"
    required property var toolState
    readonly property var layerPresenter: layerLoader.item
    readonly property bool layerModalActive: layerPresenter ? layerPresenter.modalActive : false
    signal closeRequested()
    signal actionRequested(string toolId, string fieldId, var values)
    color: LV.Theme.panelBackground05
    radius: LV.Theme.radiusXl
    border.width: 1
    border.color: LV.Theme.panelBackground08

    Flickable {
        id: viewport
        objectName: "editorDesktopPanelViewport"
        anchors.fill: parent
        anchors.margins: LV.Theme.gap16 + root.border.width
        contentWidth: width
        contentHeight: layerLoader.active ? layerLoader.height : panel.implicitHeight
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
        EditorToolPanel {
            id: panel
            visible: !layerLoader.active
            width: viewport.width
            engine: root.toolState.engine
            notice: root.toolState.notice
            definition: root.toolState.definition
            values: root.toolState.values
            sampleLayout: true
            onEdited: function(fieldId, value) { root.toolState.setValue(fieldId, value) }
            onResetRequested: root.toolState.resetTool()
            onCloseRequested: root.closeRequested()
            onColorRequested: function(fieldId) { root.toolState.chooseColor(fieldId) }
            onActionRequested: function(fieldId, label) {
                root.actionRequested(root.toolState.toolId, fieldId, root.toolState.values)
            }
        }
    }
}
