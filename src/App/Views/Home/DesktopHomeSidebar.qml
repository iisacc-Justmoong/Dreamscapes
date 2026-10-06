pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.List {
    id: root
    objectName: "desktopSidebar"
    property bool compact: false
    property bool editorLayout: false
    property string selectedAction: "home"
    readonly property real contentPadding: LV.Theme.gap8
    readonly property real horizontalInset: editorLayout && !compact ? LV.Theme.gap16 : contentPadding
    readonly property real verticalInset: editorLayout ? 0 : contentPadding
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
        LV.Spacer { minLength: root.verticalInset }
        LV.VStack {
            Layout.fillWidth: true
            Layout.leftMargin: root.horizontalInset
            Layout.rightMargin: root.horizontalInset
            spacing: 0
            Repeater {
                model: [
                    { key: "home", title: qsTr("Home"), icon: "nodeshomeFolder", asset: "home" },
                    { key: "divider1" },
                    { key: "canvas", title: qsTr("New Canvas"), icon: "imagefitContent", asset: "canvas" },
                    { key: "image", title: qsTr("Image"), icon: "unconditionalImageGeneration", asset: "image" },
                    { key: "video", title: qsTr("Video"), icon: "imageToVideo", asset: "video" },
                    { key: "audio", title: qsTr("Audio"), icon: "volume", asset: "audio" },
                    { key: "board", title: qsTr("Board"), icon: "pattern", asset: "board" },
                    { key: "tools", title: qsTr("Tools"), icon: "collection", asset: "tools" },
                    { key: "divider2" },
                    { key: "files", title: qsTr("Files"), icon: "sqlFile" },
                    { key: "assets", title: qsTr("Assets"), icon: "asset-library", asset: "assets" },
                    { key: "history", title: qsTr("Generation History"), icon: "profileCPU", asset: "history" }
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
                        iconSource: entry.modelData.asset && entry.modelData.key !== "home"
                            ? Qt.resolvedUrl("Assets/Desktop/" + entry.modelData.asset + ".svg") : ""
                        showIconSlot: entry.modelData.key !== "home" && !(root.editorLayout && entry.modelData.key === "files")
                        keyVisible: false
                        showChevron: false
                        leftPadding: entry.modelData.key === "home" || (root.editorLayout && entry.modelData.key === "files")
                            ? LV.Theme.scaleMetric(30) : LV.Theme.gap4
                        // Figma places this 16px image at the top-left of its 18px slot.
                        LV.IconButton {
                            visible: entry.modelData.key === "home"
                            x: LV.Theme.gap4
                            y: LV.Theme.scaleMetric(3)
                            width: LV.Theme.scaleMetric(16)
                            height: width
                            iconSize: LV.Theme.scaleMetric(16)
                            iconSource: Qt.resolvedUrl("Assets/Desktop/home.svg")
                            horizontalPadding: 0
                            verticalPadding: 0
                            enabled: false
                            backgroundColorDisabled: "transparent"
                            Accessible.ignored: true
                        }
                        LV.IconButton {
                            objectName: "desktopFilesIcon"
                            visible: root.editorLayout && entry.modelData.key === "files"
                            x: LV.Theme.gap4
                            y: LV.Theme.scaleRealMetric(4.1875 - (18 - 15.6659) / 2)
                            width: LV.Theme.scaleMetric(18)
                            height: width
                            iconSize: LV.Theme.scaleMetric(18)
                            iconSource: Qt.resolvedUrl("Assets/Desktop/files.svg")
                            // Fit the original vector height inside LVRS's square icon slot.
                            scale: 15.6659 / 18
                            horizontalPadding: 0
                            verticalPadding: 0
                            enabled: false
                            backgroundColorDisabled: "transparent"
                            Accessible.ignored: true
                        }
                        state: root.selectedAction === entry.modelData.key ? selectedState : defaultState
                        backgroundColor: isSelected ? LV.Theme.accentBlueMuted : "transparent"
                        backgroundColorHover: isSelected ? LV.Theme.accentBlueMuted : LV.Theme.surfaceAlt
                        backgroundColorPressed: LV.Theme.accentBlueMuted
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
        LV.Spacer { minLength: root.verticalInset }
    }
}
