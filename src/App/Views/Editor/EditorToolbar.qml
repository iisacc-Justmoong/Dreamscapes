pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Rectangle {
    id: root
    objectName: "editorToolbar"
    property bool mobileLayout: false
    implicitWidth: LV.Theme.scaleMetric(36) + LV.Theme.gap8 * 2
    implicitHeight: mobileLayout ? LV.Theme.scaleMetric(84)
        : tools.length * LV.Theme.scaleMetric(36) + Math.max(0, tools.length - 1) * LV.Theme.gap4 + LV.Theme.gap8 * 2
    radius: LV.Theme.scaleMetric(18)
    color: "transparent"
    border.width: mobileLayout ? 1 : 0
    border.color: "transparent"
    readonly property string currentTool: tools[toolList.currentIndex].key
    readonly property int currentIndex: toolList.currentIndex
    signal toolSelected(string toolId)

    // Figma 1105:91844 uses a vertical desktop rail; mobile keeps its bottom strip.
    // Both layouts share the original 22px icon assets and the 18 active tools.
    readonly property var tools: [
        { key: "elements", label: qsTr("Elements"), iconWidth: 18.1, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "text", label: qsTr("Text"), iconWidth: 14.4333, iconHeight: 18.1, iconX: 3.7833, iconY: 1.95 },
        { key: "camera-photo", label: qsTr("Camera/Photo"), iconWidth: 16.2667, iconHeight: 16.2667, iconX: 2.8667, iconY: 2.8667 },
        { key: "file", label: qsTr("File"), iconWidth: 13.5167, iconHeight: 19.9333, iconX: 4.7, iconY: 1.0333 },
        { key: "background", label: qsTr("Background"), iconWidth: 18.1, iconHeight: 16.2667, iconX: 1.95, iconY: 2.8667 },
        { key: "audio-track", label: qsTr("Audio track"), iconWidth: 16.2667, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "canvas", label: qsTr("Canvas"), iconWidth: 18.1, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "generative", label: qsTr("Generative"), iconWidth: 20.85, iconHeight: 19.9333, iconX: 0.1167, iconY: 1.0333 },
        { key: "layers", label: qsTr("Layers"), iconWidth: 18.1003, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "select", label: qsTr("Select"), iconWidth: 18.1, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "color", label: qsTr("Color"), iconWidth: 18.1, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "effects", label: qsTr("Effects"), iconWidth: 18.1, iconHeight: 18.1, iconX: 1.0333, iconY: 1.0333 },
        { key: "retouch", label: qsTr("Retouch"), iconWidth: 16.5352, iconHeight: 14.9703, iconX: 1.6816, iconY: 1.6816 },
        { key: "fill", label: qsTr("Fill"), iconWidth: 16.725, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "brush", label: qsTr("Brush"), iconWidth: 17.1833, iconHeight: 17.1833, iconX: 1.95, iconY: 2.8667 },
        { key: "auto-enhance", label: qsTr("Adjust"), iconWidth: 18.1, iconHeight: 18.1, iconX: 1.95, iconY: 1.95 },
        { key: "masking", label: qsTr("Masking"), iconWidth: 19.9333, iconHeight: 14.4333, iconX: 1.0333, iconY: 3.7833 },
        { key: "eraser", label: qsTr("Eraser"), iconWidth: 16.2667, iconHeight: 15.35, iconX: 2.8667, iconY: 3.7833 }
    ]

    function selectTool(index) {
        if (index < 0 || index >= tools.length) return
        toolList.currentIndex = index
        toolList.positionViewAtIndex(index, ListView.Contain)
        if (toolList.currentItem) toolList.currentItem.forceActiveFocus()
        toolSelected(tools[index].key)
    }
    function syncTool(index) {
        if (index < 0 || index >= tools.length) return
        toolList.currentIndex = index
        toolList.positionViewAtIndex(index, ListView.Contain)
    }
    function moveFocus(index, step) { selectTool(Math.max(0, Math.min(tools.length - 1, index + step))) }
    function focusBoundary(last) { selectTool(last ? tools.length - 1 : 0) }

    ListView {
        id: toolList
        objectName: "editorToolList"
        x: LV.Theme.gap8 + root.border.width
        y: LV.Theme.gap8 + root.border.width
        width: Math.max(0, parent.width - x * 2)
        height: root.mobileLayout ? LV.Theme.scaleMetric(68) : Math.max(0, root.height - y * 2)
        orientation: root.mobileLayout ? ListView.Horizontal : ListView.Vertical
        spacing: LV.Theme.gap4
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

        delegate: LV.Tab {
            id: tool
            required property int index
            required property var modelData
            objectName: "editorTool-" + modelData.key
            width: LV.Theme.scaleMetric(root.mobileLayout ? 86 : 36)
            height: LV.Theme.scaleMetric(root.mobileLayout ? 68 : 36)
            text: modelData.label
            iconName: modelData.key
            iconSource: Qt.resolvedUrl("Assets/" + modelData.key + ".svg")
            selected: toolList.currentIndex === index
            navigationBar: root
            tabIndex: index
            tabStyle: LV.Tab.Surface
            cornerRadius: LV.Theme.radiusLg
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

            background: Rectangle {
                radius: tool.cornerRadius
                color: tool.selected ? LV.Theme.panelBackground10
                    : tool.down ? LV.Theme.panelBackground08
                    : tool.hovered ? LV.Theme.panelBackground06 : "transparent"
                border.width: tool.selected ? 1 : 0
                border.color: LV.Theme.panelBackground08
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
                        x: LV.Theme.scaleRealMetric(tool.modelData.iconX)
                        y: LV.Theme.scaleRealMetric(tool.modelData.iconY)
                        width: LV.Theme.scaleRealMetric(tool.modelData.iconWidth)
                        height: LV.Theme.scaleRealMetric(tool.modelData.iconHeight)
                        source: tool.iconSource
                        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                        fillMode: Image.PreserveAspectFit
                        Accessible.ignored: true
                    }
                }
                LV.Label {
                    objectName: "editorToolLabel"
                    visible: root.mobileLayout
                    x: LV.Theme.gap4
                    y: LV.Theme.scaleMetric(43)
                    width: LV.Theme.scaleMetric(78)
                    height: LV.Theme.scaleMetric(11)
                    text: tool.text
                    style: caption
                    font.pixelSize: LV.Theme.scaleMetric(9)
                    font.weight: Font.Medium
                    font.styleName: "Medium"
                    lineHeight: LV.Theme.scaleMetric(11)
                    horizontalAlignment: Text.AlignHCenter
                    color: tool.selected ? LV.Theme.textTokenTitleHeader : LV.Theme.textTokenCaption
                    Accessible.ignored: true
                }
            }
        }
    }
}
