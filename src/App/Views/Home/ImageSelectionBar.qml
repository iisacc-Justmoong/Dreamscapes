pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV

LV.HStack {
    id: root
    property var entries: []
    property var selectionBackend: null
    property bool selectionMode: selectionBackend ? selectionBackend.selectionMode : false
    property var selectedKeys: []
    property string anchorKey: ""
    property bool toggleOnClick: false
    readonly property var images: entries.filter(entry => NativeData.selection.isImage(entry))
    readonly property var selectedImages: selectionBackend ? selectionBackend.selectedImages
        : images.filter(entry => selectedKeys.indexOf(NativeData.selection.key(entry)) >= 0)
    signal openRequested(var images)
    spacing: LV.Theme.gap6
    visible: selectionBackend ? entries.length > 0 : images.length > 1 || selectionMode
    onSelectionModeChanged: if (selectionBackend && !selectionMode) toggleOnClick = false
    function isSelected(entry) { return selectionBackend ? selectionBackend.isSelected(entry) : selectionMode && selectedKeys.indexOf(NativeData.selection.key(entry)) >= 0 }
    function toggle(entry) {
        if (selectionBackend) { selectionBackend.toggle(entry); return }
        if (!NativeData.selection.isImage(entry)) return
        const key = NativeData.selection.key(entry)
        selectedKeys = selectedKeys.indexOf(key) >= 0
            ? selectedKeys.filter(value => value !== key) : selectedKeys.concat([key])
    }
    function selectAt(index, modifiers, previousIndex) {
        if (selectionBackend) { selectionBackend.selectAt(index, modifiers); return }
        const entry = entries[index]
        if (!NativeData.selection.isImage(entry)) return
        const key = NativeData.selection.key(entry)
        const additive = (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
        const range = (modifiers & Qt.ShiftModifier) !== 0
        if ((additive || range) && !selectionMode) {
            const previous = entries[previousIndex]
            selectedKeys = NativeData.selection.isImage(previous) ? [NativeData.selection.key(previous)] : []
            anchorKey = NativeData.selection.isImage(previous) ? NativeData.selection.key(previous) : key
            toggleOnClick = false
            selectionMode = true
        }
        if (range) {
            let anchor = entries.findIndex(value => NativeData.selection.key(value) === anchorKey)
            if (anchor < 0) { anchor = index; anchorKey = key }
            const keys = entries.slice(Math.min(anchor, index), Math.max(anchor, index) + 1)
                .filter(value => NativeData.selection.isImage(value)).map(value => NativeData.selection.key(value))
            selectedKeys = additive ? Array.from(new Set(selectedKeys.concat(keys))) : keys
        } else if (additive || (selectionMode && toggleOnClick)) {
            toggle(entry); anchorKey = key
        } else {
            selectedKeys = [key]; anchorKey = key
        }
    }
    function selectAll() {
        if (selectionBackend) { selectionBackend.selectAll(); return }
        toggleOnClick = false
        selectedKeys = images.map(entry => NativeData.selection.key(entry))
        anchorKey = selectedKeys[0] || ""
        selectionMode = true
    }
    function reset() {
        if (selectionBackend) { selectionBackend.clearSelection(); toggleOnClick = false; return }
        selectionMode = false; selectedKeys = []; anchorKey = ""; toggleOnClick = false
    }
    onEntriesChanged: if (!selectionBackend) selectedKeys = selectedKeys.filter(key => images.some(entry => NativeData.selection.key(entry) === key))
    LV.LabelButton {
        objectName: root.objectName + "Toggle"
        text: root.selectionMode ? qsTr("Cancel") : qsTr("Select")
        tone: LV.AbstractButton.Borderless
        onClicked: {
            if (root.selectionMode) root.reset()
            else if (root.selectionBackend) { root.selectionBackend.selectionMode = true; root.toggleOnClick = true }
            else { root.selectionMode = true; root.toggleOnClick = true; root.selectedKeys = []; root.anchorKey = "" }
        }
    }
    LV.LabelButton {
        objectName: root.objectName + "Open"
        visible: root.selectionBackend ? root.selectedImages.length > 0 : root.selectionMode
        text: qsTr("Edit (%1)").arg(root.selectedImages.length)
        tone: root.selectionBackend ? LV.AbstractButton.Default : LV.AbstractButton.Primary
        enabled: root.selectedImages.length > 0
        onClicked: root.openRequested(root.selectedImages)
    }
}
