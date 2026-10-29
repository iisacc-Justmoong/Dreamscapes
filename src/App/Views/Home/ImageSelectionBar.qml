pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV

LV.HStack {
    id: root
    property var entries: []
    property var selectionBackend: null
    property alias selectionMode: nativeSelection.selectionMode
    property alias selectedKeys: nativeSelection.selectedKeys
    property alias anchorKey: nativeSelection.anchorKey
    property alias toggleOnClick: nativeSelection.toggleOnClick
    readonly property alias images: nativeSelection.images
    readonly property alias selectedImages: nativeSelection.selectedImages
    NativeImageSelection {
        id: nativeSelection
        objectName: root.objectName + "NativeModel"
        backend: root.selectionBackend
        entries: root.entries
    }
    signal openRequested(var images)
    spacing: LV.Theme.gap6
    visible: selectionBackend ? entries.length > 0 : images.length > 1 || selectionMode
    function isSelected(entry) { const revision = nativeSelection.selectedKeys; return nativeSelection.isSelected(entry) }
    function toggle(entry) { nativeSelection.toggle(entry) }
    function selectAt(index, modifiers, previousIndex) { nativeSelection.selectAt(index, modifiers, previousIndex === undefined ? -1 : previousIndex) }
    function selectAll() { nativeSelection.selectAll() }
    function reset() { nativeSelection.reset() }
    LV.LabelButton {
        objectName: root.objectName + "Toggle"
        text: root.selectionMode ? qsTr("Cancel") : qsTr("Select")
        tone: LV.AbstractButton.Borderless
        onClicked: nativeSelection.toggleMode()
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
