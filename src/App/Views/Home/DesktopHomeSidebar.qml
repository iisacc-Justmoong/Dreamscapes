pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Style 1.0 as Style

LV.List {
    id: root
    objectName: "desktopSidebar"
    property bool compact: false
    property string selectedAction: "home"
    readonly property real contentPadding: Style.Tokens.metric("Theme Metrics/Space/8")
    signal actionRequested(string action)
    items: [({})]
    footerVisible: false
    minimumListHeight: 0
    itemHeight: 0
    listWidth: LV.Theme.scaleMetric(compact ? 48 : 181)
    backgroundColor: "transparent"

    function reveal(row) {
        let viewport = row.parent
        while (viewport && !("contentY" in viewport && "contentHeight" in viewport))
            viewport = viewport.parent
        if (!viewport) return
        const top = row.mapToItem(viewport, 0, 0).y
        const offset = top < contentPadding ? top - contentPadding
            : top + row.height > viewport.height - contentPadding
                ? top + row.height - viewport.height + contentPadding : 0
        viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height,
            viewport.contentY + offset))
    }

    itemDelegate: LV.VStack {
        required property var modelData
        spacing: 0
        LV.Spacer { minLength: root.contentPadding }
        LV.VStack {
            Layout.fillWidth: true
            Layout.leftMargin: root.contentPadding
            Layout.rightMargin: root.contentPadding
            spacing: 0
            Repeater {
                model: [
                    { key: "home", title: qsTr("Home"), icon: "nodeshomeFolder" },
                    { key: "divider1" },
                    { key: "canvas", title: qsTr("New Canvas"), icon: "imagefitContent" },
                    { key: "image", title: qsTr("Image"), icon: "unconditionalImageGeneration" },
                    { key: "video", title: qsTr("Video"), icon: "imageToVideo" },
                    { key: "audio", title: qsTr("Audio"), icon: "volume" },
                    { key: "board", title: qsTr("Board"), icon: "pattern" },
                    { key: "tools", title: qsTr("Tools"), icon: "collection" },
                    { key: "divider2" },
                    { key: "files", title: qsTr("Files"), icon: "sqlFile" },
                    { key: "assets", title: qsTr("Assets"), icon: "asset-library" },
                    { key: "history", title: qsTr("Generation History"), icon: "profileCPU" }
                ]
                delegate: LV.VStack {
                    id: entry
                    required property var modelData
                    readonly property bool divider: modelData.key.indexOf("divider") === 0
                    Layout.fillWidth: true
                    spacing: 0
                    LV.MenuDivider {
                        objectName: "desktopDivider_" + entry.modelData.key
                        Layout.fillWidth: true
                        visible: entry.divider
                    }
                    LV.MenuItem {
                        id: action
                        objectName: "desktopAction_" + entry.modelData.key
                        Layout.fillWidth: true
                        visible: !entry.divider
                        itemWidth: 0
                        label: root.compact ? "" : entry.modelData.title || ""
                        iconSize: 18
                        iconName: entry.modelData.icon || ""
                        showIconSlot: entry.modelData.key !== "home"
                        keyVisible: false
                        showChevron: false
                        leftPadding: entry.modelData.key === "home"
                            ? LV.Theme.scaleMetric(30) : Style.Tokens.metric("Theme Metrics/Space/4")
                        // Figma places this 16px image at the top-left of its 18px slot.
                        LV.IconButton {
                            visible: entry.modelData.key === "home"
                            x: Style.Tokens.metric("Theme Metrics/Space/4")
                            y: LV.Theme.scaleMetric(3)
                            width: LV.Theme.scaleMetric(16)
                            height: width
                            iconSize: LV.Theme.scaleMetric(16)
                            iconSource: LV.Theme.iconPath("nodeshomeFolder")
                            horizontalPadding: 0
                            verticalPadding: 0
                            enabled: false
                            backgroundColorDisabled: "transparent"
                            Accessible.ignored: true
                        }
                        state: root.selectedAction === entry.modelData.key ? selectedState : defaultState
                        backgroundColor: isSelected ? Style.Tokens.color("Theme Colors/Accent/Blue/Selected") : "transparent"
                        backgroundColorHover: isSelected ? Style.Tokens.color("Theme Colors/Accent/Blue/Selected") : Style.Tokens.color("Theme Colors/Panel/surfaceAlt")
                        backgroundColorPressed: Style.Tokens.color("Theme Colors/Accent/Blue/Selected")
                        Accessible.name: entry.modelData.title || ""
                        Accessible.role: Accessible.Button
                        LV.Tooltip {
                            target: action
                            automatic: false
                            visible: root.compact && (action.hovered || action.activeFocus)
                            text: entry.modelData.title || ""
                        }
                        onActiveFocusChanged: if (activeFocus) root.reveal(this)
                        onClicked: root.actionRequested(entry.modelData.key)
                    }
                }
            }
        }
        LV.Spacer { minLength: root.contentPadding }
    }
}
