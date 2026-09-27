pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "canvasEditor"
    property url imageSource: ""
    property var generationResult: ({})
    property bool mobileLayout: false
    readonly property string selectedTool: bottomToolbar.currentTool
    signal backRequested()
    signal toolSelected(string toolId)
    signal toolActionRequested(string toolId, string fieldId, var values)
    onVisibleChanged: if (!visible) toolSheet.dismiss()
    onMobileLayoutChanged: if (!mobileLayout) toolSheet.dismiss()

    LV.HStack {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: LV.Theme.gap10
        height: implicitHeight
        spacing: LV.Theme.gap12

        LV.LabelButton {
            objectName: "editorBackButton"
            text: qsTr("Back")
            tone: LV.AbstractButton.Default
            onClicked: root.backRequested()
        }
        LV.Label {
            text: qsTr("Editor")
            style: header
            Layout.fillWidth: true
        }
        LV.Label {
            text: qsTr("Untitled Canvas")
            style: description
        }
    }

    Item {
        id: workspace
        objectName: "editorWorkspace"
        anchors.top: toolbar.bottom
        anchors.bottom: bottomToolbar.visible ? bottomToolbar.top : parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: LV.Theme.gap16
        clip: true

        Rectangle {
            objectName: "editorBlankCanvas"
            visible: root.imageSource.toString().length === 0
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height)
            height: width
            color: "white"
        }
        Image {
            objectName: "editorCanvasImage"
            anchors.fill: parent
            source: root.imageSource
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }
    }
    EditorToolbar {
        id: bottomToolbar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: LV.Theme.gap8
        anchors.rightMargin: LV.Theme.gap8
        anchors.bottomMargin: LV.Theme.gap8
        height: implicitHeight
        visible: root.mobileLayout
        onToolSelected: function(toolId) {
            root.toolSelected(toolId)
            if (root.mobileLayout && root.visible) toolSheet.openTool(toolId)
        }
    }
    EditorToolSheet {
        id: toolSheet
        parent: root
        onActionRequested: function(toolId, fieldId, values) { root.toolActionRequested(toolId, fieldId, values) }
    }
    Keys.onEscapePressed: {
        if (toolSheet.visible) toolSheet.dismiss()
        else root.backRequested()
    }
}
