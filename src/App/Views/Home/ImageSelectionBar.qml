pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import "ImageSelection.js" as Selection

LV.HStack {
    id: root
    property var entries: []
    property bool selectionMode: false
    property var selectedKeys: []
    property string anchorKey: ""
    property bool toggleOnClick: false
    readonly property var images: entries.filter(entry => Selection.isImage(entry))
    readonly property var selectedImages: images.filter(entry => selectedKeys.indexOf(Selection.source(entry)) >= 0)
    signal openRequested(var images)
    spacing: LV.Theme.gap6
    visible: images.length > 1 || selectionMode
    function isSelected(entry) { return selectionMode && selectedKeys.indexOf(Selection.source(entry)) >= 0 }
    function toggle(entry) {
        if (!Selection.isImage(entry)) return
        const key = Selection.source(entry)
        selectedKeys = selectedKeys.indexOf(key) >= 0
            ? selectedKeys.filter(value => value !== key) : selectedKeys.concat([key])
    }
    function selectAt(index, modifiers, previousIndex) {
        const entry = entries[index]
        if (!Selection.isImage(entry)) return
        const key = Selection.source(entry)
        const additive = (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
        const range = (modifiers & Qt.ShiftModifier) !== 0
        if ((additive || range) && !selectionMode) {
            const previous = entries[previousIndex]
            selectedKeys = Selection.isImage(previous) ? [Selection.source(previous)] : []
            anchorKey = Selection.isImage(previous) ? Selection.source(previous) : key
            toggleOnClick = false
            selectionMode = true
        }
        if (range) {
            let anchor = entries.findIndex(value => Selection.source(value) === anchorKey)
            if (anchor < 0) { anchor = index; anchorKey = key }
            const keys = entries.slice(Math.min(anchor, index), Math.max(anchor, index) + 1)
                .filter(value => Selection.isImage(value)).map(value => Selection.source(value))
            selectedKeys = additive ? Array.from(new Set(selectedKeys.concat(keys))) : keys
        } else if (additive || (selectionMode && toggleOnClick)) {
            toggle(entry); anchorKey = key
        } else {
            selectedKeys = [key]; anchorKey = key
        }
    }
    function selectAll() {
        toggleOnClick = false
        selectedKeys = images.map(entry => Selection.source(entry))
        anchorKey = selectedKeys[0] || ""
        selectionMode = true
    }
    function reset() { selectionMode = false; selectedKeys = []; anchorKey = ""; toggleOnClick = false }
    onEntriesChanged: selectedKeys = selectedKeys.filter(key => images.some(entry => Selection.source(entry) === key))
    LV.LabelButton {
        objectName: root.objectName + "Toggle"
        text: root.selectionMode ? qsTr("Cancel") : qsTr("Select")
        tone: LV.AbstractButton.Borderless
        onClicked: { if (root.selectionMode) root.reset(); else { root.selectionMode = true; root.toggleOnClick = true; root.selectedKeys = []; root.anchorKey = "" } }
    }
    LV.LabelButton {
        objectName: root.objectName + "Open"
        visible: root.selectionMode
        text: qsTr("Edit (%1)").arg(root.selectedImages.length)
        tone: LV.AbstractButton.Primary
        enabled: root.selectedImages.length > 0
        onClicked: root.openRequested(root.selectedImages)
    }
}
