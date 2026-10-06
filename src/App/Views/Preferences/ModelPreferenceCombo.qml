pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

LV.ComboBox {
    id: root
    property var models: []
    property string selectedModel: ""
    property string settingLabel: ""
    readonly property var entries: [{ id: "", name: qsTr("Automatic") }].concat(models)
    readonly property int selectedIndex: entries.findIndex(function(entry) { return entry.id === root.selectedModel })
    signal modelSelected(string modelId)
    text: models.length === 0 && selectedModel.length === 0 ? qsTr("No models available")
        : selectedIndex >= 0 ? entries[selectedIndex].name : qsTr("Unavailable: %1").arg(selectedModel)
    arrow: LV.Stepper.Down
    implicitHeight: LV.Theme.scaleMetric(28)
    enabled: models.length > 0 || selectedModel.length > 0
    activeFocusOnTab: true
    Accessible.role: Accessible.ComboBox
    Accessible.name: settingLabel
    Accessible.description: text
    function openMenu() {
        if (!enabled) return
        menu.openFor(root, 0, height)
    }
    onClicked: openMenu()
    Keys.onSpacePressed: openMenu()
    Keys.onReturnPressed: openMenu()
    Keys.onEnterPressed: openMenu()
    Keys.onDownPressed: openMenu()
    onVisibleChanged: if (!visible) menu.close()

    LV.ContextMenu {
        id: menu
        objectName: "modelPreferenceMenu"
        items: root.entries
        showIconSlot: false
        compactItems: false
        itemWidth: Math.max(0, Math.min(root.width - leftPadding - rightPadding,
            parent ? parent.width - leftPadding - rightPadding - edgeMargin * 2 : root.width))
        selectedIndex: root.selectedIndex
        implicitHeight: Math.min(options.contentHeight + topPadding + bottomPadding,
            LV.Theme.scaleMetric(240), parent ? Math.max(0, parent.height - edgeMargin * 2) : LV.Theme.scaleMetric(240))
        onItemTriggered: function(index, entry) { root.modelSelected(entry.id) }
        onOpened: {
            options.selectedIndex = Math.max(0, root.selectedIndex)
            Qt.callLater(options.revealNamed, "modelPreferenceOption" + options.selectedIndex)
            options.forceActiveFocus()
        }
        onClosed: if (root.visible) root.forceActiveFocus()
        contentItem: PreferenceList {
            id: options
            objectName: "modelPreferenceOptions"
            expandToContent: true
            items: menu.items
            listWidth: menu.itemWidth
            clip: true
            itemSpacing: menu.itemSpacing
            onSelectedIndexChanged: Qt.callLater(options.revealNamed, "modelPreferenceOption" + selectedIndex)
            Keys.onUpPressed: selectedIndex = Math.max(0, selectedIndex - 1)
            Keys.onDownPressed: selectedIndex = Math.min(entryCount - 1, selectedIndex + 1)
            Keys.onReturnPressed: menu.triggerEntry(selectedIndex)
            Keys.onEnterPressed: menu.triggerEntry(selectedIndex)
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                    selectedIndex = event.key === Qt.Key_Home ? 0 : entryCount - 1
                    event.accepted = true
                }
            }
            LV.MenuItem {
                required property var modelData
                readonly property int index: modelData.index
                objectName: "modelPreferenceOption" + index
                itemWidth: menu.itemWidth
                label: modelData.entry.name
                keyVisible: false
                showIconSlot: false
                showChevron: false
                state: index === options.selectedIndex ? selectedState : defaultState
                Accessible.name: modelData.entry.name
                onClicked: menu.triggerEntry(index)
            }
        }
    }
}
