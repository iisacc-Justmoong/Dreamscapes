pragma ComponentBehavior: Bound
import QtQuick
import ".." as Frames
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import ".." as Shared

Column {
    id: root
    objectName: "editorGraphicPanel"
    required property var toolState
    readonly property string toolId: toolState.toolId
    readonly property alias viewCatalog: nativeDraft.viewCatalog
    property alias currentViewId: nativeDraft.currentViewId
    readonly property alias selectedView: nativeDraft.selectedView
    readonly property var engine: toolState.engine
    readonly property bool modalActive: sourcePicker.visible || previewSheet.visible || operationSheet.visible || colorSheet.visible || collectionSheet.visible
    readonly property alias previewImage: nativeDraft.previewImage
    readonly property alias originalImage: nativeDraft.originalImage
    property alias operationStatus: nativeDraft.operationStatus
    readonly property var statusView: statusFor(operationStatus)
    property alias collectionIndices: nativeDraft.collectionIndices
    readonly property alias samplingColor: nativeDraft.samplingColor
    readonly property alias imageBusy: nativeDraft.imageBusy
    readonly property alias busy: nativeDraft.busy
    property alias resultMessage: nativeDraft.resultMessage
    NativeGraphicDraft {
        id: nativeDraft
        objectName: "editorNativeGraphicDraft"
        catalog: NativeData
        toolState: root.toolState.nativeModel
        onSheetsCloseRequested: { previewSheet.close(); operationSheet.close() }
        onSourceRequested: sourcePicker.open()
        onColorRequested: color => { colorSheet.open(); colorSheet.loadedContent.setColor(color) }
        onPreviewRequested: previewSheet.open()
        onOperationSheetRequested: operationSheet.open()
        onCloseToolRequested: if (root.toolState.visible) root.toolState.close()
        onReopenToolRequested: if (root.toolState.presentation === LV.Sheet.Mobile) root.toolState.open()
    }
    width: 398
    spacing: 12

    function defaultView() { const revision = nativeDraft.revision; return nativeDraft.defaultView() }
    function restoreView() { nativeDraft.restoreView() }
    function selectView(id) { resultMessage = ""; return nativeDraft.selectView(id) }
    function operationFor(row) { const revision = nativeDraft.revision; return nativeDraft.operationFor(row) }
    function rowsOf(rows) { const revision = nativeDraft.revision; return nativeDraft.rowsOf(rows) }
    function parameters(operation) { const revision = nativeDraft.revision; return nativeDraft.parameters(operation) }
    function parameterPath(row) { const revision = nativeDraft.revision; return nativeDraft.parameterPath(row) }
    function value(row) { const revision = nativeDraft.revision; return nativeDraft.value(row) }
    function collectionDefinition() { const revision = nativeDraft.revision; return nativeDraft.collectionDefinition() }
    function collectionItems() { const revision = nativeDraft.revision; return nativeDraft.collectionItems() }
    function collectionItem() { const revision = nativeDraft.revision; return nativeDraft.collectionItem() }
    function selectCollection(index) { nativeDraft.selectCollection(index) }
    function addCollectionItem() { nativeDraft.addCollectionItem() }
    function removeCollectionItem() { nativeDraft.removeCollectionItem() }
    function edit(row, next) { nativeDraft.edit(row, next); resultMessage = "" }
    function commitField(row) { nativeDraft.commitField(row) }
    function resetView() { nativeDraft.resetView(); resultMessage = "" }
    function activate(option, row) { operationSheet.close(); nativeDraft.activate(option,row || null) }
    function applyReturnedParameters(api, result) { nativeDraft.applyReturnedParameters(api,result) }
    function statusFor(status) { const revision = nativeDraft.revision; return nativeDraft.statusFor(status) }
    function helperContext() { return nativeDraft.helperContext() }
    function imageContext() { return nativeDraft.imageContext() }
    function execute(action, operation, supplied) { nativeDraft.execute(action,operation || "",supplied) }
    function swatchColor() { const revision = nativeDraft.revision; return nativeDraft.swatchColor() }
    function curveChannel() { const revision = nativeDraft.revision; return nativeDraft.curveChannel() }
    function curvePoints(row) { const revision = nativeDraft.revision; return nativeDraft.curvePoints(row) }
    function asset(name) { return Qt.resolvedUrl("Assets/Graphic/" + (name === "adjust" ? "Adjustment" : name) + ".svg") }
    onToolIdChanged: { previewSheet.close(); operationSheet.close(); colorSheet.close(); collectionSheet.close(); restoreView() }
    Component.onCompleted: restoreView()

    Item {
        width: parent.width
        height: 22
        Flickable {
            id: tabsViewport
            width: parent.width - (root.collectionDefinition() ? 60 : 30)
            height: parent.height
            contentWidth: tabs.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Row {
                id: tabs
                spacing: 4
                Repeater {
                    model: root.selectedView ? root.selectedView.navigation : []
                    LV.IconButton {
                        id: tab
                        required property var modelData
                        objectName: "graphicNavigate-" + modelData.target
                        width: 22; height: 22
                        horizontalPadding: 2; verticalPadding: 2
                        iconSource: root.asset(modelData.icon || "adjust")
                        iconSize: 18
                        tone: LV.AbstractButton.Borderless
                        Accessible.name: modelData.label
                        Accessible.checkable: true
                        Accessible.checked: modelData.selected
                        onClicked: root.selectView(modelData.target)

                        EditorButtonState { button: tab; selected: Boolean(modelData.selected) }
                        Accessible.onToggleAction: tab.click()
                    }
                }
            }
        }
        LV.LabelButton {
            visible: root.collectionDefinition() !== null
            objectName: "graphicManageCollection"
            x: parent.width - 52; width: 22; height: 22
            text: "+"
            Accessible.name: qsTr("Manage items")
            onClicked: collectionSheet.open()
        }
        LV.IconButton {
            objectName: "graphicResetView"
            x: parent.width - width
            width: 22; height: 22
            horizontalPadding: 2; verticalPadding: 2
            iconSource: root.asset("reset")
            iconSize: 16
            tone: LV.AbstractButton.Borderless
            Accessible.name: qsTr("Reset view")
            onClicked: root.resetView()
        }
    }
    Repeater {
        model: root.selectedView ? root.selectedView.rows : []
        EditorGraphicRow {
            required property var modelData
            entry: modelData
            presenter: root
            width: root.width
        }
    }
    LV.Label {
        width: parent.width
        visible: text.length > 0
        height: visible ? implicitHeight : 0
        text: root.resultMessage
        style: caption
        wrapMode: Text.Wrap
    }
    FileDialog {
        id: sourcePicker
        objectName: "editorGraphicSourcePicker"
        title: qsTr("Choose a source image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"), qsTr("All files (*)")]
        onAccepted: nativeDraft.acceptSource(selectedFile)
    }
    Frames.PanelSheet {
        id: colorSheet
        objectName: "editorGraphicColorSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile
        title: qsTr("Color")
        contentComponent: Shared.ColorPicker {
            onAccepted: function(color) {
                nativeDraft.acceptColor(color)
                colorSheet.close()
            }
            onCanceled: colorSheet.close()
        }
    }
    Frames.PanelSheet {
        id: collectionSheet
        objectName: "editorGraphicCollectionSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile
        title: qsTr("Items")
        contentComponent: Column {
            width: 320; spacing: 8
            LV.Label { width: parent.width; text: qsTr("Item") + " " + ((root.collectionIndices[(root.collectionDefinition() || {}).path] || 0) + 1) + " / " + root.collectionItems().length }
            Row {
                width: parent.width; spacing: 8
                LV.LabelButton { width: (parent.width - 24) / 4; height: 30; text: qsTr("Previous"); onClicked: root.selectCollection((root.collectionIndices[(root.collectionDefinition() || {}).path] || 0) - 1) }
                LV.LabelButton { width: (parent.width - 24) / 4; height: 30; text: qsTr("Next"); onClicked: root.selectCollection((root.collectionIndices[(root.collectionDefinition() || {}).path] || 0) + 1) }
                LV.LabelButton { objectName: "graphicAddItem"; width: (parent.width - 24) / 4; height: 30; text: qsTr("Add"); onClicked: root.addCollectionItem() }
                LV.LabelButton { objectName: "graphicRemoveItem"; width: (parent.width - 24) / 4; height: 30; text: qsTr("Remove"); onClicked: root.removeCollectionItem() }
            }
        }
    }
    Frames.PanelSheet {
        id: operationSheet
        objectName: "editorGraphicOperationSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile
        title: root.statusView ? root.statusView.title : root.resultMessage
        contentComponent: Column {
            width: 398; spacing: 12
            Repeater {
                model: root.statusView ? root.statusView.rows : []
                EditorGraphicRow { required property var modelData; entry: modelData; presenter: root; width: parent.width }
            }
            LV.Label { width: parent.width; text: root.resultMessage; wrapMode: Text.Wrap; style: caption }
        }
    }
    Frames.PanelSheet {
        id: previewSheet
        objectName: "editorGraphicPreviewSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile
        title: qsTr("Preview")
        contentComponent: Column {
            width: 398; spacing: 12
            Repeater {
                model: root.statusFor("preview") ? NativeData.presentation.range(root.statusFor("preview").rows, 0, -1) : []
                EditorGraphicRow { required property var modelData; entry: modelData; presenter: root; width: parent.width }
            }
            Row {
                width: parent.width; height: 260; spacing: 8
                Column {
                    width: (parent.width - 8) / 2; spacing: 8
                    LV.Label { width: parent.width; text: qsTr("Before"); style: caption }
                    Image { width: parent.width; height: 230; source: root.originalImage; fillMode: Image.PreserveAspectFit }
                }
                Column {
                    width: (parent.width - 8) / 2; spacing: 8
                    LV.Label { width: parent.width; text: qsTr("After"); style: caption }
                    Image { width: parent.width; height: 230; source: root.previewImage; fillMode: Image.PreserveAspectFit }
                }
            }
        }
    }
}
