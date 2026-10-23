pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import ".." as Shared
import "EditorGraphicDefinitions.js" as Panels
import "EditorGraphicState.js" as State

Column {
    id: root
    objectName: "editorGraphicPanel"
    required property var toolState
    readonly property string toolId: toolState.toolId
    readonly property var viewCatalog: Panels.panelsFor(toolId)
    property string currentViewId: ""
    readonly property var selectedView: Panels.byId(currentViewId)
    readonly property var engine: toolState.engine
    readonly property bool modalActive: sourcePicker.visible || previewSheet.visible || operationSheet.visible || colorSheet.visible || collectionSheet.visible
    property url previewImage: ""
    property url originalImage: ""
    property string operationStatus: ""
    readonly property var statusView: statusFor(operationStatus)
    property string sourceParameter: ""
    property string sourceOperation: ""
    property var collectionIndices: ({})
    property var pickerRow: null
    property string pickerApi: ""
    property bool samplingColor: false
    property int sampleSequence: 0
    property var collectionIndicesByOperation: ({})
    property string indicesOperation: ""
    readonly property bool imageBusy: imageCommand.state.pending
    property bool imageAction: false
    property var imageOrigin: ({})
    readonly property bool busy: imageBusy || settingsHelper.state.pending
    property bool helperAction: false
    property bool preparingHelper: false
    property var helperOrigin: ({})
    property int imageCommandEpoch: 0
    property string resultMessage: ""
    width: 398
    spacing: 12

    function defaultView() {
        const family = toolId === "masking" ? "mask" : toolId === "auto-enhance" ? "adjust" : toolId
        return Panels.catalog().defaults[family] || (viewCatalog.length ? viewCatalog[0].id : "")
    }
    function restoreView() {
        const saved = toolState.graphicViewsByTool[toolId]
        currentViewId = saved && Panels.byId(saved) ? saved : defaultView()
    }
    function selectView(id) {
        const panel = Panels.byId(id)
        if (!panel) return false
        const targetTool = panel.tool === "mask" ? "masking" : panel.tool === "adjust" ? "auto-enhance" : panel.tool
        const next = Object.assign({}, toolState.graphicViewsByTool)
        next[targetTool] = panel.id
        toolState.graphicViewsByTool = next
        if (targetTool !== toolId) toolState.toolId = targetTool
        currentViewId = panel.id
        resultMessage = ""
        return true
    }
    function operationFor(row) { return row.operation || (row.sourceArgument && row.api ? row.api : selectedView.operation) }
    function rowsOf(rows) {
        let result = []
        rows.forEach(function(row) { result.push(row); if (row.rows) result = result.concat(root.rowsOf(row.rows)) })
        return result
    }
    function parameters(operation) {
        const drafts = toolState.values.graphicDrafts || {}
        const currentDefaults = selectedView && selectedView.operation === operation ? Panels.defaultsFor(currentViewId) : {}
        return State.nested(Object.assign({}, Panels.operationDefaults(operation), currentDefaults), drafts[operation] || {})
    }
    function parameterPath(row) {
        let path = State.resolvedPath(row.sdkParameter || row.parameter || "", collectionIndices)
        if (selectedView && selectedView.name.indexOf("PointColor") >= 0 && !row.collection) path = path.replace("points[0]", "points[" + (collectionIndices.points || 0) + "]")
        return path
    }
    function value(row) {
        if (row.uiOnly && row.editorTarget) return "Sample " + ((collectionIndices.points || 0) + 1)
        const all = toolState.values.graphicDrafts || {}
        const draft = all[operationFor(row)] || {}
        const key = parameterPath(row)
        if (key && draft[key] !== undefined) return draft[key]
        if (key && !row.uiOnly) {
            const found = State.getPath(State.nested(draft), key)
            if (found !== undefined) return found
        }
        if (row.collection) {
            const definition = collectionDefinition()
            const found = definition && key.startsWith(definition.path + "[") ? State.getPath(collectionItems(), key.slice(definition.path.length)) : undefined
            if (found !== undefined) return found
        }
        return row.initial
    }
    function collectionDefinition() {
        if (!selectedView) return null
        const field = rowsOf(selectedView.rows).find(function(row) { return Boolean(row.collection) })
        if (!field) return null
        let editor = null
        Panels.catalog().panels.some(function(panel) {
            editor = root.rowsOf(panel.rows).concat(panel.availableEditors || []).find(function(row) { return row.target === root.currentViewId && row.editor })
            return Boolean(editor)
        })
        return {path: State.resolvedPath(field.collection.path, collectionIndices), minimum: editor && editor.minItems || 0, maximum: editor && editor.maxItems || 65536, initial: editor && editor.initial || []}
    }
    function collectionItems() {
        const definition = collectionDefinition()
        const found = definition ? State.getPath(parameters(selectedView.operation), definition.path) : []
        const items = Array.isArray(found) ? State.clone(found) : []
        // The SDK represents an identity Camera Raw curve as an empty array.
        // Show editable endpoints without storing a draft until the user edits.
        if (!items.length && definition && definition.minimum >= 2) {
            const template = collectionItem()
            if (Array.isArray(template) && template.length === 2) return [[0, 0], [255, 255]]
            if (template.input !== undefined && template.output !== undefined) return [{input: 0, output: 0}, {input: 1, output: 1}]
        }
        return items
    }
    function collectionItem() {
        const definition = collectionDefinition()
        if (!definition) return {}
        let item = {}
        rowsOf(selectedView.rows).forEach(function(row) {
            if (!row.collection || row.initial === undefined || row.uiOnly || row.sourceArgument) return
            const path = State.resolvedPath(row.parameter, collectionIndices)
            if (path.startsWith(definition.path + "[")) {
                const suffix = path.slice(path.indexOf("]", definition.path.length) + 1).replace(/^\./, "")
                if (/^\d+$/.test(State.pathParts(suffix)[0]) && !Object.keys(item).length) item = []
                if (suffix) State.setPath(item, suffix, row.initial)
            }
        })
        return item
    }
    function selectCollection(index) {
        const definition = collectionDefinition()
        if (!definition) return
        const next = Object.assign({}, collectionIndices); next[definition.path] = Math.max(0, Math.min(Math.max(0, collectionItems().length - 1), index)); collectionIndices = next
    }
    function addCollectionItem() {
        const definition = collectionDefinition()
        if (!definition) return
        const items = collectionItems()
        if (items.length >= definition.maximum) return
        let selected = items.length
        const template = collectionItem()
        const curve = Array.isArray(template) && template.length === 2 || template.input !== undefined && template.output !== undefined
        if (curve && items.length >= 2) {
            const x = function(point) { return Array.isArray(point) ? point[0] : point.input }
            const y = function(point) { return Array.isArray(point) ? point[1] : point.output }
            let gap = -1, insert = 1
            for (let i = 1; i < items.length; ++i) if (x(items[i]) - x(items[i - 1]) > gap) { gap = x(items[i]) - x(items[i - 1]); insert = i }
            let input = (x(items[insert - 1]) + x(items[insert])) / 2
            if (Array.isArray(template)) input = Math.floor(input)
            if (input <= x(items[insert - 1]) || input >= x(items[insert])) return
            const output = (y(items[insert - 1]) + y(items[insert])) / 2
            items.splice(insert, 0, Array.isArray(template) ? [input, Math.round(output)] : {input: input, output: output})
            selected = insert
        } else if (!items.length && Array.isArray(definition.initial) && definition.initial.length) { items.push(...State.clone(definition.initial)); selected = 0 }
        else items.push(template)
        toolState.replaceGraphicCollection(selectedView.operation, definition.path, items)
        selectCollection(selected)
    }
    function removeCollectionItem() {
        const definition = collectionDefinition()
        const items = collectionItems()
        if (!definition || items.length <= definition.minimum) return
        items.splice(collectionIndices[definition.path] || 0, 1)
        toolState.replaceGraphicCollection(selectedView.operation, definition.path, items)
        selectCollection(collectionIndices[definition.path] || 0)
    }
    function edit(row, next) {
        if (!row.parameter) return
        if (row.uiOnly) {
            if (row.editorTarget) { const selected = Number(String(next).replace("Sample ", "")) - 1; const indices = Object.assign({}, collectionIndices); indices.points = Math.max(0, selected); collectionIndices = indices }
            return
        }
        if (value(row) === next) return
        if (row.collection) {
            const definition = collectionDefinition(), items = collectionItems()
            const index = collectionIndices[definition.path] || 0
            while (items.length <= index) items.push(collectionItem())
            if (items.length < definition.minimum && Array.isArray(definition.initial) && definition.initial.length >= definition.minimum) items.splice(0, items.length, ...State.clone(definition.initial))
            while (items.length < definition.minimum) {
                const endpoint = collectionItem()
                if (Array.isArray(endpoint)) { endpoint[0] = 255; endpoint[1] = 255 }
                else if (endpoint.input !== undefined) { endpoint.input = 1; endpoint.output = 1 }
                items.push(endpoint)
            }
            toolState.replaceGraphicCollection(operationFor(row), definition.path, items)
        }
        toolState.setGraphicValue(operationFor(row), parameterPath(row), next)
        resultMessage = ""
    }
    function commitField(row) {
        if (row.sourceArgument && row.api === "refineMask") execute("mask-helper", "refineMask", {amount: value(row)})
    }
    function resetView() {
        if (!selectedView) return
        const defaults = Panels.defaultsFor(currentViewId)
        rowsOf(selectedView.rows).forEach(function(row) { if (row.collection && row.initial !== undefined && !row.uiOnly && !row.sourceArgument) defaults[root.parameterPath(row)] = row.initial })
        toolState.resetGraphicView(selectedView.operation, defaults)
        resultMessage = ""
    }
    function activate(option, row) {
        operationSheet.close()
        const action = option.action || option.value || (row ? row.action : "") || ""
        if (action === "preview" || action === "apply") { execute(action); return }
        if (action === "reset") { resetView(); return }
        if (action === "overview") { const overview = Panels.overviewFor(toolId); selectView(option.target || selectedView.parent || (overview ? overview.id : defaultView())); return }
        if (option.target) { selectView(option.target); return }
        const api = option.api || (row ? row.api : "")
        if (row && row.uiOnly && api === "whiteBalancePreset") {
            if (option.value !== null && option.value !== undefined) execute("helper", api, {preset: option.value})
            return
        }
        if (row && (row.editor === "hostImagePicker" || row.inputType === "image" || row.inputType === "file")) {
            sourceParameter = parameterPath(row); sourceOperation = operationFor(row); sourcePicker.open(); return
        }
        if (row && row.sourceArgument && row.parameter === "sample") {
            pickerRow = row; pickerApi = api
            sampleSequence = engine.layerColorSample.sequence || 0
            samplingColor = engine.beginLayerColorSample()
            resultMessage = samplingColor ? qsTr("Click a visible color on the canvas") : engine.error
            if (samplingColor && toolState.visible) toolState.close()
            return
        }
        if (row && row.editor === "hostColorPicker") {
            pickerRow = row; pickerApi = row.sourceArgument ? api : ""
            colorSheet.open()
            const current = State.getPath(parameters(operationFor(row)), parameterPath(row)) || value(row)
            const color = Array.isArray(current) ? Qt.rgba(current[0], current[1], current[2], current.length > 3 ? current[3] : 1) : typeof current === "string" && current.charAt(0) === "#" ? current.split(" · ")[0] : "#808080"
            colorSheet.loadedContent.setColor(color)
            return
        }
        if (row && row.sourceArgument) {
            if (api === "combineMasks") { execute("mask-helper", api, {operation: option.value}); return }
            if (api === "invertMask") { execute("mask-helper", api, {}); return }
            let args = {}
            if (row.parameter === "options") {
                const index = collectionIndices.points || 0
                const points = parameters(selectedView.operation).points || []
                args = points[index] || {}
                execute("preview", api, args); return
            }
            execute("helper", api, args); return
        }
        if (row && row.parameter && option.value !== undefined) edit(row, option.value)
    }
    function applyReturnedParameters(api, result) {
        if (!result.parameters || !Object.keys(result.parameters).length) return
        const target = api === "autoDevelopLight" ? "developLight" : api === "autoWhiteBalance" || api === "sampledWhiteBalance" || api === "whiteBalancePreset" ? "whiteBalance" : api === "autoBlackAndWhiteMix" || api === "samplePointColor" ? "developColor" : api === "estimateChromaticAberration" ? "correctOptics" : selectedView.operation
        const prefix = api === "samplePointColor" ? "points[" + (collectionIndices.points || 0) + "]" : ""
        toolState.setGraphicValues(target, State.flatten(result.parameters, prefix))
    }
    function statusFor(status) {
        const family = Panels.normalizedTool(toolId) === "mask" ? "adjust" : Panels.normalizedTool(toolId)
        const states = Panels.catalog().statusViews[family] || {}
        return status ? Panels.byId(states[status]) : null
    }
    function helperContext() {
        return {tool: toolId, view: currentViewId, indices: collectionIndices,
            draft: helperAction && selectedView ? parameters(selectedView.operation) : ({})}
    }
    function finishHelper() {
        if (!helperAction || preparingHelper || settingsHelper.state.pending) return
        // Keep a detached value while applying the returned settings changes
        // the owner context and clears its previous result.
        const result = JSON.parse(JSON.stringify(settingsHelper.state))
        const current = helperOrigin.engine === engine && settingsHelper.contextMatches(helperContext())
        helperAction = false
        if (!current || !result.valid) {
            resultMessage = current ? result.error || "" : ""
            operationStatus = resultMessage ? engine && engine.documentReady ? "error" : "empty" : ""
            return
        }
        applyReturnedParameters(result.operation, result)
        resultMessage = qsTr("Settings updated")
        operationStatus = "applied"
    }
    EditorGraphicHelper {
        id: settingsHelper
        objectName: "editorGraphicHelper"
        canvas: root.engine
        context: root.helperContext()
        onChanged: Qt.callLater(root.finishHelper)
    }
    Connections {
        target: root.toolState
        function onValuesChanged() { ++root.imageCommandEpoch }
    }
    function execute(action, operation, supplied) {
        if (!engine || busy || !selectedView) return
        previewSheet.close(); operationSheet.close()
        operationStatus = "processing"
        resultMessage = qsTr("Processing…")
        const api = operation || selectedView.operation
        const args = supplied === undefined ? parameters(api) : supplied
        if (action === "helper") {
            preparingHelper = true
            helperAction = true
            helperOrigin = {engine: engine}
            settingsHelper.request(api, args)
            preparingHelper = false
            finishHelper()
            return
        }
        imageAction = true
        imageOrigin = {engine: engine, action: action, api: api}
        imageCommand.request(action, toolId, api, args)
        finishImage()
    }
    function imageContext() {
        return {tool: toolId, view: currentViewId, epoch: imageCommandEpoch, indices: collectionIndices}
    }
    function finishImage() {
        if (!imageAction || imageCommand.state.pending) return
        const result = Object.assign({}, imageCommand.state)
        const origin = imageOrigin
        const current = origin.engine === engine && imageCommand.contextMatches(imageContext())
        imageAction = false
        if (!current || !result.valid) {
            resultMessage = current ? result.error || "" : ""
            operationStatus = resultMessage ? engine && engine.documentReady ? "error" : "empty" : ""
            return
        }
        if (origin.action === "preview") {
            const preview = result.imageSource
            const original = result.originalImageSource || preview
            applyReturnedParameters(origin.api, result)
            previewImage = preview; originalImage = original; previewSheet.open()
        }
        resultMessage = origin.action === "preview" ? qsTr("Preview ready") : qsTr("Applied")
        operationStatus = origin.action === "preview" ? "preview" : "applied"
        if (origin.action !== "preview" && origin.action !== "mask-helper") operationSheet.open()
    }
    EditorGraphicCommand {
        id: imageCommand
        objectName: "editorGraphicCommand"
        canvas: root.engine
        context: root.imageContext()
        onChanged: Qt.callLater(root.finishImage)
    }
    function swatchColor() {
        if (!selectedView || !selectedView.sourceParameter) return "transparent"
        const value = State.getPath(parameters(selectedView.operation), State.resolvedPath(selectedView.sourceParameter, collectionIndices))
        return Array.isArray(value) ? Qt.rgba(value[0] || 0, value[1] || 0, value[2] || 0, value.length > 3 ? value[3] : 1) : "transparent"
    }
    function curveChannel() { return selectedView ? selectedView.name.match(/Red|Green|Blue/) : null }
    function curvePoints(row) {
        const settings = parameters(selectedView.operation)
        const fields = rowsOf(selectedView.rows)
        const input = fields.find(function(field) { return field.parameter && /\.(input)$|\[\]\[0\]/.test(field.parameter) })
        if (!input) return row.points || [[0, 0], [1, 1]]
        let path = input.parameter.replace(/\[\d*\]\.(input)$/, "").replace(/\[\]\[0\]$/, "")
        path = State.resolvedPath(path, collectionIndices)
        let points = State.getPath(settings, path)
        if (!Array.isArray(points) || points.length < 2) return row.points || [[0, 0], [1, 1]]
        const scale = input.max === 255 ? 255 : 1
        return points.map(function(point) { return Array.isArray(point) ? [point[0] / scale, point[1] / scale] : [point.input / scale, point.output / scale] })
    }
    function asset(name) { return Qt.resolvedUrl("Assets/Graphic/" + (name === "adjust" ? "Adjustment" : name) + ".svg") }
    onSelectedViewChanged: {
        if (!selectedView || indicesOperation === selectedView.operation) return
        const all = Object.assign({}, collectionIndicesByOperation)
        if (indicesOperation) all[indicesOperation] = collectionIndices
        collectionIndicesByOperation = all
        indicesOperation = selectedView.operation
        collectionIndices = all[indicesOperation] || {}
    }
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
    Connections {
        target: root.engine
        function onToolStateChanged() {
            if (!root.samplingColor || root.engine.layerColorSample.picking || root.engine.layerColorSample.pending) return
            root.samplingColor = false
            const sample = root.engine.layerColorSample
            if (sample.error) { root.resultMessage = sample.error; return }
            if (!sample.valid || sample.sequence === root.sampleSequence) return
            const color = sample.color
            root.execute("helper", root.pickerApi, {sample: [color.r, color.g, color.b, color.a]})
            if (root.toolState.presentation === LV.Sheet.Mobile) root.toolState.open()
        }
    }
    FileDialog {
        id: sourcePicker
        objectName: "editorGraphicSourcePicker"
        title: qsTr("Choose a source image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"), qsTr("All files (*)")]
        onAccepted: root.toolState.setGraphicValue(root.sourceOperation, root.sourceParameter, selectedFile.toString())
    }
    LV.Sheet {
        id: colorSheet
        objectName: "editorGraphicColorSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile
        title: qsTr("Color")
        contentComponent: Shared.ColorPicker {
            onAccepted: function(color) {
                const selected = color, row = root.pickerRow
                const channels = row && (root.selectedView.sourceType === "Rgba" || String(row.initial).indexOf("100%") >= 0 || row.sourceArgument) ? [selected.r, selected.g, selected.b, selected.a] : [selected.r, selected.g, selected.b]
                if (root.pickerApi) root.execute("helper", root.pickerApi, {sample: channels})
                else if (row) root.toolState.setGraphicValue(root.operationFor(row), root.parameterPath(row), channels)
                colorSheet.close()
            }
            onCanceled: colorSheet.close()
        }
    }
    LV.Sheet {
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
    LV.Sheet {
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
    LV.Sheet {
        id: previewSheet
        objectName: "editorGraphicPreviewSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile
        title: qsTr("Preview")
        contentComponent: Column {
            width: 398; spacing: 12
            Repeater {
                model: root.statusFor("preview") ? root.statusFor("preview").rows.slice(0, -1) : []
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
