pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import LVRS 1.0 as LV
import Dreamscapes.Style 1.0 as Style

Rectangle {
    id: root
    objectName: "editorToolbar"
    property bool mobileLayout: false
    property bool sharedBorders: false
    readonly property Item panelFrame: Frames.PanelFrame {
        parent: root; panelId: "tools"
        topEdge: !root.sharedBorders; bottomEdge: !root.sharedBorders; leftEdge: !root.sharedBorders
    }
    implicitWidth: mobileLayout ? 52 : 30
    implicitHeight: mobileLayout ? 84 : tools.length * 22 + Math.max(0, tools.length - 1) * 4 + 15 + 16
    radius: 0
    color: "transparent"
    border.width: mobileLayout ? 1 : 0
    border.color: "transparent"
    readonly property string currentTool: tools[Math.max(0, toolList.currentIndex)].key
    readonly property int currentIndex: toolList.currentIndex
    signal toolSelected(string toolId)

    readonly property var tools: [
        { key: "elements", icon: "rectangle", label: qsTr("Elements") },
        { key: "text", icon: "text", label: qsTr("Text") },
        { key: "camera-photo", icon: "panel-camera-live", label: qsTr("Camera/Photo") },
        { key: "file", icon: "document-properties", label: qsTr("File") },
        { key: "background", icon: "background", label: qsTr("Background") },
        { key: "audio-track", icon: "audio-track", label: qsTr("Audio track") },
        { key: "canvas", icon: "canvas-size", label: qsTr("Canvas") },
        { key: "generative", icon: "spark", label: qsTr("Generative") },
        { key: "layers", icon: "panel-layers-stack", label: qsTr("Layers") },
        { key: "select", icon: "direct-select", label: qsTr("Select") },
        { key: "color", icon: "color", label: qsTr("Color") },
        { key: "effects", icon: "effect-stack", label: qsTr("Effects") },
        { key: "retouch", icon: "panel-retouch-heal", label: qsTr("Retouch") },
        { key: "fill", icon: "fill", label: qsTr("Fill") },
        { key: "brush", icon: "brush", label: qsTr("Brush") },
        { key: "auto-enhance", icon: "auto-tone", label: qsTr("Adjust") },
        { key: "masking", icon: "layer-mask", label: qsTr("Masking") },
        { key: "eraser", icon: "eraser", label: qsTr("Eraser") }
    ]

    function focusCurrent() {
        const current = toolList.currentItem
        if (current && current.button) current.button.forceActiveFocus()
    }
    function selectTool(index) {
        if (index < 0 || index >= tools.length) return
        toolList.currentIndex = index
        toolList.positionViewAtIndex(index, ListView.Contain)
        focusCurrent()
        toolSelected(tools[index].key)
    }
    function syncTool(index) {
        if (index < 0 || index >= tools.length) return
        let focused = toolList.Window.window ? toolList.Window.window.activeFocusItem : null
        let retainFocus = false
        while (focused) {
            if (focused === toolList) { retainFocus = true; break }
            focused = focused.parent
        }
        toolList.currentIndex = index
        toolList.positionViewAtIndex(index, ListView.Contain)
        if (retainFocus) focusCurrent()
    }
    function moveFocus(index, step) { selectTool(Math.max(0, Math.min(tools.length - 1, index + step))) }
    function focusBoundary(last) { selectTool(last ? tools.length - 1 : 0) }

    ListView {
        id: toolList
        objectName: "editorToolList"
        x: root.mobileLayout ? 9 : 4
        y: root.mobileLayout ? 9 : 8
        width: Math.max(0, parent.width - x * 2)
        height: root.mobileLayout ? 68 : Math.max(0, root.height - y * 2)
        orientation: root.mobileLayout ? ListView.Horizontal : ListView.Vertical
        spacing: Style.Tokens.metric("Theme Metrics/Space/4")
        model: root.tools
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: root.mobileLayout ? Flickable.HorizontalFlick : Flickable.VerticalFlick
        snapMode: root.mobileLayout ? ListView.SnapToItem : ListView.NoSnap
        highlightFollowsCurrentItem: false
        currentIndex: 0
        onWidthChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        onHeightChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        onOrientationChanged: Qt.callLater(function() {
            toolList.contentX = 0
            toolList.contentY = 0
            toolList.positionViewAtIndex(toolList.currentIndex, ListView.Contain)
        })
        delegate: Item {
            id: host
            required property int index
            required property var modelData
            readonly property int groupOffset: !root.mobileLayout && NativeData.presentation.indexOf([4, 9, 12], index) >= 0 ? 5 : 0
            readonly property var button: buttonLoader.item
            width: root.mobileLayout ? 86 : 22
            height: (root.mobileLayout ? 68 : 22) + groupOffset
            Rectangle {
                visible: host.groupOffset > 0
                x: 1; width: 20; height: 1
                color: Style.Tokens.color("Theme Colors/Panel/surfacePanel")
            }
            Loader {
                id: buttonLoader
                y: host.groupOffset
                width: host.width
                height: root.mobileLayout ? 68 : 22
                sourceComponent: root.mobileLayout ? mobileButton : desktopButton
                Component {
                    id: desktopButton
                    LV.IconButton {
                        id: desktopTool
                        objectName: "editorTool-" + host.modelData.key
                        iconName: host.modelData.icon
                        iconSource: LV.Theme.iconPath(iconName)
                        tone: LV.AbstractButton.Default
                        autoExclusive: true
                        readonly property bool selected: toolList.currentIndex === host.index
                        EditorButtonState { button: desktopTool; selected: desktopTool.selected }
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: host.modelData.label
                        onClicked: root.selectTool(host.index)
                        Keys.onUpPressed: root.moveFocus(host.index, -1)
                        Keys.onDownPressed: root.moveFocus(host.index, 1)
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                root.focusBoundary(event.key === Qt.Key_End)
                                event.accepted = true
                            }
                        }
                        LV.Tooltip { target: desktopTool; text: host.modelData.label }
                    }
                }
                Component {
                    id: mobileButton
                    LV.Tab {
            id: tool
            readonly property int index: host.index
            readonly property var modelData: host.modelData
            objectName: "editorTool-" + modelData.key
            width: LV.Theme.scaleMetric(root.mobileLayout ? 86 : 36)
            height: LV.Theme.scaleMetric(root.mobileLayout ? 68 : 36)
            text: modelData.label
            iconName: modelData.icon
            iconSource: LV.Theme.iconPath(iconName)
            selected: toolList.currentIndex === index
            navigationBar: root
            tabIndex: index
            tabStyle: LV.Tab.Surface
            cornerRadius: Style.Tokens.metric("Theme Metrics/Radius/12")
            horizontalPadding: 0
            verticalPadding: 0
            bottomPadding: 0
            onClicked: root.selectTool(index)
            Keys.onUpPressed: function(event) {
                event.accepted = !root.mobileLayout
                if (event.accepted) root.moveFocus(index, -1)
            }
            Keys.onDownPressed: function(event) {
                event.accepted = !root.mobileLayout
                if (event.accepted) root.moveFocus(index, 1)
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                    root.focusBoundary(event.key === Qt.Key_End)
                    event.accepted = true
                } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                    root.moveFocus(index,event.key === Qt.Key_Left ? -1 : 1)
                    event.accepted = true
                }
            }


            background: Rectangle {
                radius: tool.cornerRadius
                color: tool.selected ? Style.Tokens.color("Theme Colors/Panel/surfaceInput")
                    : tool.down ? Style.Tokens.color("Theme Colors/Panel/surfacePanel")
                    : tool.hovered ? Style.Tokens.color("Theme Colors/Panel/surfaceAlt") : "transparent"
                border.width: tool.selected ? 1 : 0
                border.color: Style.Tokens.color("Theme Colors/Panel/surfacePanel")
            }
            contentItem: Item {
                Item {
                    objectName: "editorToolIconSlot"
                    x: LV.Theme.scaleMetric(root.mobileLayout ? 32 : 7)
                    y: LV.Theme.scaleMetric(root.mobileLayout ? 16 : 8)
                    width: LV.Theme.scaleMetric(22)
                    height: width
                    Image {
                        objectName: "editorToolIcon"
                        anchors.centerIn: parent
                        width: LV.Theme.iconSm
                        height: LV.Theme.iconSm
                        source: tool.iconSource
                        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                        fillMode: Image.PreserveAspectFit
                        Accessible.ignored: true
                    }
                }
                LV.Label {
                    objectName: "editorToolLabel"
                    visible: root.mobileLayout
                    x: Style.Tokens.metric("Theme Metrics/Space/4")
                    y: LV.Theme.scaleMetric(43)
                    width: LV.Theme.scaleMetric(78)
                    height: LV.Theme.scaleMetric(11)
                    text: tool.text
                    style: caption
                    font.pixelSize: Style.Tokens.metric("Theme Typography/Caption/textCaption")
                    font.weight: Style.Tokens.metric("Theme Typography/Weight/Medium")
                    font.styleName: LV.Theme.textBodyStyleName
                    lineHeight: Style.Tokens.metric("Theme Typography/Caption/textCaptionLineHeight")
                    horizontalAlignment: Text.AlignHCenter
                    color: tool.selected ? Style.Tokens.color("Theme Colors/Text/titleHeaderColor") : Style.Tokens.color("Theme Colors/Text/captionColor")
                    Accessible.ignored: true
                }
            }
        }
                }
            }
        }
    }
}
