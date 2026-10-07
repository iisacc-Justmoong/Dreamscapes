pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import ".." as Views
import "EditorToolDefinitions.js" as Definitions

Item {
    id: root
    objectName: "editorLayersPanel"
    required property var toolState
    readonly property var engine: toolState.engine
    property int panelIndex: 0
    property string channel: "RGB"
    property bool composite: true
    property bool shadowClipping: false
    property bool highlightClipping: false
    property var histogram: ({red: [], green: [], blue: [], pixelCount: 0, mean: 0, standardDeviation: 0})
    property var collapsedGroups: ({})
    readonly property var selected: engine ? engine.selectedLayer : ({})
    readonly property var info: engine ? engine.layerPanelInfo : ({})
    readonly property var sample: engine ? engine.layerColorSample : ({valid: false, hex: "No sample", rgb: "", color: "transparent"})
    readonly property bool modalActive: adjustmentSheet.visible
    property string adjustmentKey: ""
    property string lastAdjustmentTool: "color"
    property var adjustmentValues: ({})
    readonly property var adjustments: [
        {key: "contrast", label: qsTr("Contrast"), icon: "Contrast", fields: ["field-4", "field-5"]},
        {key: "exposure", label: qsTr("Exposure"), icon: "Exposure", fields: ["field-1", "field-2"]},
        {key: "levels", label: qsTr("Levels"), icon: "Levels", fields: ["field-16", "field-20", "field-17", "field-21"]},
        {key: "curves", label: qsTr("Curves"), icon: "Curves", fields: ["curveAmount", "field-45", "field-46"]},
        {key: "shadows", label: qsTr("Shadows / highlights"), icon: "Shadows", fields: ["field-12", "field-8"]},
        {key: "auto", label: qsTr("Auto tone"), icon: "AutoTone", fields: []},
        {key: "hue", label: qsTr("Hue / saturation"), icon: "Saturation", fields: ["field-36", "field-42", "field-43"]},
        {key: "vibrance", label: qsTr("Vibrance"), icon: "Vibrance", fields: ["field-32"]},
        {key: "white", label: qsTr("White balance"), icon: "WhiteBalance", fields: ["field-25", "field-28", "field-31"]},
        {key: "mono", label: qsTr("Black & white"), icon: "BlackWhite", fields: []},
        {key: "gradient", label: qsTr("Gradient map"), icon: "Gradient", fields: [], reason: qsTr("Gradient maps are not supported by the editing engine.")},
        {key: "lookup", label: qsTr("Color lookup"), icon: "ProofColors", fields: [], reason: qsTr("Color LUT import is not supported by the editing engine.")}
    ]
    implicitWidth: 398
    implicitHeight: Math.max(525, layout.implicitHeight)
    function asset(name) { return Qt.resolvedUrl("Assets/Layers/" + name + ".svg") }
    function report(success) { toolState.notice = success ? "" : engine.error }
    function refreshHistogram() { if (engine && panelIndex === 2) histogram = engine.layerHistogram(composite, channel) }
    function toggleVisibility(row) { report(row.group ? engine.setLayerGroupVisible(row.layerId, !row.visible) : engine.setLayerVisible(row.layerId, !row.visible)) }
    function editAdjustment(fieldId, value) {
        if (!engine.applyToolField("color", fieldId, value)) { report(false); return }
        const next = Object.assign({}, adjustmentValues); next[fieldId] = value; adjustmentValues = next
        const all = Object.assign({}, toolState.settingsByTool); all.color = next; toolState.settingsByTool = all
        lastAdjustmentTool = "color"
    }
    function openAdjustment(entry) {
        if (entry.key === "auto") { lastAdjustmentTool = "auto-enhance"; report(engine.executeToolAction("auto-enhance", "field-3", Definitions.defaults(Definitions.tool("auto-enhance")))); return }
        if (entry.key === "mono") { editAdjustment("field-36", -100); return }
        adjustmentKey = entry.key
        adjustmentValues = Object.assign({}, Definitions.defaults(Definitions.tool("color")), {"field-1": 0, "field-2": 0, "field-4": 0, "field-8": 0, "field-12": 0, "field-16": 0, "field-17": 0, "field-20": 0, "field-21": 0, "field-25": 0, "field-28": 0, "field-32": 0, "field-36": 0, "field-42": 0, "field-43": 0, "curveAmount": 0}, engine.toolValues("color"))
        adjustmentSheet.title = entry.label
        adjustmentSheet.open()
    }
    function action(key) {
        if (key === "sample") { const started = engine.beginLayerColorSample(); report(started); if (started && toolState.visible) toolState.close() }
        else if (key === "copyColor") report(engine.copyLayerSampleColor())
        else if (key === "copyDetails") report(engine.copyLayerPanelDetails())
        else if (key === "reveal") report(engine.revealLayerSource())
        else if (key === "refresh") refreshHistogram()
        else if (key === "shadow") { shadowClipping = !shadowClipping; engine.setLayerHistogramClipping(shadowClipping, highlightClipping) }
        else if (key === "highlight") { highlightClipping = !highlightClipping; engine.setLayerHistogramClipping(shadowClipping, highlightClipping) }
        else if (["RGB", "Red", "Green", "Blue"].indexOf(key) >= 0) channel = key
        else openAdjustment(adjustments.find(function(entry) { return entry.key === key }))
    }
    onPanelIndexChanged: {
        histogramRefresh.restart()
        if (panelIndex !== 2 && engine) { shadowClipping = false; highlightClipping = false; engine.setLayerHistogramClipping(false, false) }
        if (panelIndex !== 3 && engine && sample.picking) engine.cancelLayerColorSample()
    }
    onChannelChanged: histogramRefresh.restart()
    onCompositeChanged: histogramRefresh.restart()
    Connections {
        target: root.engine
        function onRevisionChanged() { histogramRefresh.restart() }
        function onFrameChanged() { histogramRefresh.restart() }
        function onSelectionChanged() { histogramRefresh.restart() }
    }
    Timer { id: histogramRefresh; interval: 60; onTriggered: root.refreshHistogram() }

    component Heading: Views.PanelRow {
        type: LV.ListItem.Mini
        showLeadingIcon: false
        iconSize: 13
        minItemWidth: 0
        miniItemWidth: 0
        Layout.fillWidth: true
    }
    component ScopeRow: RowLayout {
        property string label
        property string value
        signal opened(var target)
        spacing: 8
        Heading { label: parent.label; Layout.fillWidth: true }
        LV.ComboBox { text: parent.value; Layout.preferredWidth: Math.min(206, root.width * 0.52); implicitHeight: 20; arrow: LV.Stepper.Down; onClicked: parent.opened(this) }
    }
    component ActionTile: Item {
        id: tile
        required property var entry
        implicitHeight: 72
        height: 72
        HoverHandler { id: tileHover }
        Controls.ToolTip.visible: tileHover.hovered && Boolean(entry.reason)
        Controls.ToolTip.text: entry.reason || ""
        LV.IconButton {
            objectName: "layerAction-" + tile.entry.key
            anchors.top: parent.top; anchors.topMargin: 4; anchors.horizontalCenter: parent.horizontalCenter
            width: 34; height: 34; horizontalPadding: 8; verticalPadding: 8
            iconSize: 18; iconSource: root.asset(tile.entry.icon)
            tone: tile.entry.active ? LV.AbstractButton.Primary : LV.AbstractButton.Default
            enabled: tile.entry.enabled === undefined ? Boolean(root.info.canAdjust) && !tile.entry.reason : tile.entry.enabled
            Accessible.name: tile.entry.label
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.text: tile.entry.reason || tile.entry.label
            onClicked: root.action(tile.entry.key)
        }
        LV.Label {
            objectName: "layerActionLabel-" + tile.entry.key
            anchors.bottom: parent.bottom; anchors.bottomMargin: 4; anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 4; anchors.rightMargin: 4
            height: 26; style: body; color: tile.entry.color || LV.Theme.bodyColor
            text: tile.entry.label; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap; lineHeightMode: Text.FixedHeight; lineHeight: 13
        }
    }
    component ActionGrid: Column {
        id: grid
        required property var entries
        property int preferredColumns: 3
        readonly property int columns: width < 300 ? 2 : preferredColumns
        spacing: 8
        Repeater {
            model: Math.ceil(grid.entries.length / grid.columns)
            Row {
                id: actionRow
                required property int index
                width: grid.width; height: 72; spacing: 8
                Repeater {
                    model: grid.entries.slice(actionRow.index * grid.columns, (actionRow.index + 1) * grid.columns)
                    ActionTile { required property var modelData; entry: modelData; width: (grid.width - (grid.columns - 1) * 8) / grid.columns }
                }
            }
        }
    }
    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: 12
        LV.IconSegmentedControl {
            objectName: "layerPanelSegments"
            forceBorderlessTone: false
            LV.IconButton {
                objectName: "layerTab-layer"; Accessible.name: qsTr("Layer")
                tone: root.panelIndex === 0 ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                onClicked: root.panelIndex = 0
                contentItem: Item {
                    implicitWidth: 18; implicitHeight: 18
                    Image { x: 3.03; y: 0.12; width: 9.93952; height: 9.93952; rotation: 45; source: root.asset("Rectangle1474"); sourceSize: Qt.size(40, 40) }
                    Image { x: 3.06; y: 3.38; width: 9.87522; height: 9.87522; rotation: 45; source: root.asset("Rectangle1475"); sourceSize: Qt.size(40, 40) }
                    Image { x: 3.06; y: 6.61; width: 9.87522; height: 9.87522; rotation: 45; source: root.asset("Rectangle1475"); sourceSize: Qt.size(40, 40) }
                }
            }
            LV.IconButton { objectName: "layerTab-adjust"; Accessible.name: qsTr("Adjust"); iconSource: root.asset("AdjustmentLayer"); tone: root.panelIndex === 1 ? LV.AbstractButton.Default : LV.AbstractButton.Borderless; onClicked: root.panelIndex = 1 }
            LV.IconButton { objectName: "layerTab-histogram"; Accessible.name: qsTr("Histogram"); iconSource: root.asset("ProofColors"); tone: root.panelIndex === 2 ? LV.AbstractButton.Default : LV.AbstractButton.Borderless; onClicked: root.panelIndex = 2 }
            LV.IconButton { objectName: "layerTab-information"; Accessible.name: qsTr("Information"); iconSource: root.asset("StatusInfoOutline"); iconSize: 16; horizontalPadding: 3; verticalPadding: 3; tone: root.panelIndex === 3 ? LV.AbstractButton.Default : LV.AbstractButton.Borderless; onClicked: root.panelIndex = 3 }
        }
        LV.Label { Layout.fillWidth: true; visible: root.toolState.notice.length > 0; text: root.toolState.notice; style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
        Loader {
            Layout.fillWidth: true; Layout.fillHeight: true
            sourceComponent: root.panelIndex === 0 ? layerView : root.panelIndex === 1 ? adjustView : root.panelIndex === 2 ? histogramView : informationView
        }
    }
    Component {
        id: layerView
        ColumnLayout {
            spacing: 12
            Heading { label: root.selected.id ? root.selected.name + " · " + root.info.contentType : qsTr("Select a layer") }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                ScopeRow { Layout.fillWidth: true; label: qsTr("Blend mode"); value: root.selected.blend || qsTr("Normal"); enabled: Boolean(root.selected.id); onOpened: function(target) { blendMenu.openFor(target, 0, target.height) } }
                RowLayout {
                    Layout.fillWidth: true
                    Heading { label: qsTr("Opacity") }
                    LV.InputField {
                        objectName: "layerOpacityInput"; Layout.preferredWidth: Math.min(206, root.width * 0.52)
                        Layout.preferredHeight: 22; fieldMinHeight: 22; clearButtonVisible: false
                        text: Math.round((root.selected.opacity === undefined ? 1 : root.selected.opacity) * 100) + "%"
                        enabled: Boolean(root.selected.id)
                        function commit() { const value = Number(text.replace("%", "")); if (root.selected.id && Number.isFinite(value) && value >= 0 && value <= 100 && value !== Math.round(root.selected.opacity * 100)) root.report(root.engine.setLayerOpacity(root.selected.id, value / 100)); text = Qt.binding(function() { return Math.round((root.selected.opacity === undefined ? 1 : root.selected.opacity) * 100) + "%" }) }
                        onAccepted: commit()
                        onActiveFocusChanged: if (!activeFocus) commit()
                    }
                }
                LV.Slider { objectName: "layerOpacitySlider"; Layout.fillWidth: true; implicitHeight: 22; size: LV.Slider.Mini; showSymbol: false; showMinMax: false; from: 0; to: 100; value: (root.selected.opacity === undefined ? 1 : root.selected.opacity) * 100; enabled: Boolean(root.selected.id); onMoved: root.report(root.engine.setLayerOpacity(root.selected.id, value / 100)) }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 328
                color: LV.Theme.panelBackground05; radius: 6; border.width: 1; border.color: LV.Theme.panelBackground08
                Flickable {
                    anchors.fill: parent; anchors.margins: 4; clip: true; contentWidth: width; contentHeight: hierarchy.implicitHeight
                    flickableDirection: Flickable.VerticalFlick; boundsBehavior: Flickable.StopAtBounds
                    LV.HierarchyList {
                        id: hierarchy
                        objectName: "layerHierarchy"
                        width: parent.width; generatedItemWidth: width; generatedRowHeight: 32; generatedIndentStep: 20; generatedIconSize: 18
                        autoExpandAncestorsOnActivate: false
                        function syncSelectedLayer() {
                            if (root.engine && root.engine.selectedLayerId)
                                activateByKey(root.engine.selectedLayerId)
                        }
                        Component.onCompleted: Qt.callLater(syncSelectedLayer)
                        Connections {
                            target: root.engine
                            function onSelectionChanged() { Qt.callLater(hierarchy.syncSelectedLayer) }
                            function onRevisionChanged() { Qt.callLater(hierarchy.syncSelectedLayer) }
                        }
                        model: {
                            if (!root.engine) return []
                            const revision = root.engine.revision, selected = root.engine.selectedLayerId, layers = root.engine.layers
                            return root.engine.layerHierarchy().map(function(row) { row.count = 0; row.iconSource = root.asset(row.locked ? "Lock" : row.kind); if (row.group) row.expanded = !root.collapsedGroups[row.layerId]; return row })
                        }
                        onActiveChanged: function(item) { if (item && item.nodeData && !item.nodeData.group && item.nodeData.layerId !== root.engine.selectedLayerId) root.report(root.engine.selectLayer(item.nodeData.layerId)) }
                        onExpansionChanged: function(item, expanded) { if (item.nodeData.group) { const next = Object.assign({}, root.collapsedGroups); next[item.nodeData.layerId] = !expanded; root.collapsedGroups = next } }
                        itemDelegate: Component {
                            LV.HierarchyItem {
                                id: layerRow
                                objectName: "layerRow-" + itemKey
                                opacity: nodeData && !nodeData.visible ? 0.5 : 1
                                iconSource: root.asset(nodeData && nodeData.locked ? "Lock" : nodeData ? nodeData.kind : "ImageGutter14X14")
                                Component.onCompleted: {
                                    iconSize = Qt.binding(function() { return nodeData && nodeData.kind === "ImageGutter14X14" && !nodeData.locked ? 14 : 18 })
                                    Qt.callLater(hierarchy.syncSelectedLayer)
                                }
                                countView: Component {
                                    LV.IconButton {
                                        objectName: "layerVisible-" + layerRow.itemKey
                                        iconSource: root.asset("Visibility"); iconSize: 16
                                        horizontalPadding: 0; verticalPadding: 0; width: 16; height: 16; tone: LV.AbstractButton.Borderless
                                        Accessible.name: qsTr("Toggle visibility for %1").arg(layerRow.text)
                                        onClicked: root.toggleVisibility(layerRow.nodeData)
                                    }
                                }
                            }
                        }
                    }
                    Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
                }
            }
            Flow {
                Layout.fillWidth: true; spacing: 8
                // Preserve each Figma footer surface when an operation is unavailable.
                LV.DropdownButton { objectName: "layerNew"; iconMode: true; tone: LV.AbstractButton.Default; backgroundColorDisabled: toneBackgroundColor; iconSource: root.asset("NewLayer"); Accessible.name: qsTr("New layer"); enabled: root.engine && root.engine.documentReady; onClicked: newMenu.openFor(this, 0, height) }
                LV.IconButton { objectName: "layerGroup"; tone: LV.AbstractButton.Default; backgroundColorDisabled: toneBackgroundColor; iconSource: root.asset("Group"); enabled: Boolean(root.info.canGroup); Accessible.name: qsTr("Group"); onClicked: root.report(root.engine.groupSelectedLayer()) }
                LV.IconButton { objectName: "layerMerge"; tone: LV.AbstractButton.Default; backgroundColorDisabled: toneBackgroundColor; iconSource: root.asset("Merge"); enabled: Boolean(root.info.canMerge); Accessible.name: qsTr("Merge down"); onClicked: root.report(root.engine.mergeSelectedDown()) }
                LV.IconButton { objectName: "layerDuplicate"; tone: LV.AbstractButton.Default; backgroundColorDisabled: toneBackgroundColor; iconSource: root.asset("DuplicateLayer"); enabled: Boolean(root.selected.id); Accessible.name: qsTr("Duplicate"); onClicked: root.report(root.engine.duplicateSelectedLayer()) }
                LV.IconButton { objectName: "layerMask"; tone: LV.AbstractButton.Default; backgroundColorDisabled: toneBackgroundColor; iconSource: root.asset("LayerMask"); enabled: root.engine && root.engine.toolState.selectionActive && !root.engine.toolState.pixelLocked; Accessible.name: qsTr("Apply selection as alpha mask"); onClicked: root.report(root.engine.executeToolAction("layers", "field-23", root.toolState.values)) }
                LV.IconButton { objectName: "layerLock"; tone: LV.AbstractButton.Default; backgroundColorDisabled: toneBackgroundColor; iconSource: root.asset("Lock"); enabled: Boolean(root.selected.id); Accessible.name: qsTr("Lock layer"); onClicked: root.toolState.setValue("field-8", root.selected.lock === "None" ? "All" : "None") }
                LV.IconButton { objectName: "layerDelete"; iconSource: root.asset("Generaldelete"); tone: LV.AbstractButton.Borderless; backgroundColorDisabled: toneBackgroundColor; enabled: Boolean(root.selected.id) && root.selected.lock === "None"; Accessible.name: qsTr("Delete layer"); onClicked: root.report(root.engine.removeLayer(root.selected.id)) }
            }
        }
    }
    Component {
        id: adjustView
        ColumnLayout {
            spacing: 12
            Heading { label: qsTr("Adjustments") }
            ScopeRow { Layout.fillWidth: true; label: qsTr("Apply to"); value: qsTr("Selected layer"); onOpened: function(target) { adjustmentScopeMenu.openFor(target, 0, target.height) } }
            ColumnLayout { Layout.fillWidth: true; spacing: 8; Heading { label: qsTr("Tone") } ActionGrid { Layout.fillWidth: true; entries: root.adjustments.slice(0, 6) } }
            ColumnLayout { Layout.fillWidth: true; spacing: 8; Heading { label: qsTr("Color") } ActionGrid { Layout.fillWidth: true; entries: root.adjustments.slice(6) } }
            Item { Layout.fillHeight: true }
            LV.LabelButton { objectName: "layerResetAdjustments"; text: qsTr("Reset adjustments"); tone: LV.AbstractButton.Borderless; enabled: Boolean(root.info.canAdjust); onClicked: root.report(root.engine.resetTool(root.lastAdjustmentTool, Definitions.defaults(Definitions.tool(root.lastAdjustmentTool)))) }
        }
    }
    Component {
        id: histogramView
        ColumnLayout {
            spacing: 12
            Heading { label: qsTr("Histogram") }
            ScopeRow { Layout.fillWidth: true; label: qsTr("Source"); value: root.composite ? qsTr("Composite") : qsTr("Selected layer"); onOpened: function(target) { sourceMenu.openFor(target, 0, target.height) } }
            ActionGrid {
                Layout.fillWidth: true; preferredColumns: 4
                entries: [ {key: "RGB", label: "RGB", icon: "ProofColors", active: root.channel === "RGB", enabled: true}, {key: "Red", label: qsTr("Red"), icon: "Levels", color: LV.Theme.accentRed, active: root.channel === "Red", enabled: true}, {key: "Green", label: qsTr("Green"), icon: "Levels", color: LV.Theme.accentGreen, active: root.channel === "Green", enabled: true}, {key: "Blue", label: qsTr("Blue"), icon: "Levels", color: LV.Theme.accentBlue, active: root.channel === "Blue", enabled: true} ]
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Heading { label: qsTr("Distribution") }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 179; color: LV.Theme.panelBackground08; radius: LV.Theme.radiusMd
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 8
                        Canvas {
                            id: plot; objectName: "layerHistogramPlot"; Layout.fillWidth: true; Layout.preferredHeight: 128
                            onWidthChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d"); ctx.clearRect(0, 0, width, height); ctx.strokeStyle = LV.Theme.panelBackground12
                                for (let y = 32; y <= 128; y += 32) { ctx.beginPath(); ctx.moveTo(0, Math.min(127, y)); ctx.lineTo(width, Math.min(127, y)); ctx.stroke() }
                                const series = [{name: "red", label: "Red", color: LV.Theme.accentRed}, {name: "green", label: "Green", color: LV.Theme.accentGreen}, {name: "blue", label: "Blue", color: LV.Theme.accentBlue}]
                                let maximum = 1
                                series.forEach(function(s) { if (root.channel === "RGB" || root.channel === s.label) (root.histogram[s.name] || []).forEach(function(v) { maximum = Math.max(maximum, v) }) })
                                series.forEach(function(s) {
                                    if (root.channel !== "RGB" && root.channel !== s.label) return
                                    const bins = root.histogram[s.name] || []; if (bins.length !== 256 || !root.histogram.pixelCount) return
                                    ctx.beginPath(); ctx.moveTo(0, 128)
                                    for (let i = 0; i < 256; ++i) ctx.lineTo(i / 255 * width, 128 - bins[i] / maximum * 118)
                                    ctx.lineTo(width, 128); ctx.closePath(); ctx.fillStyle = s.color; ctx.fill()
                                })
                            }
                            Connections { target: root; function onHistogramChanged() { plot.requestPaint() } }
                        }
                        RowLayout { Layout.fillWidth: true; Repeater { model: ["0", "64", "128", "192", "255"]; LV.Label { required property string modelData; Layout.fillWidth: true; text: modelData; style: caption; horizontalAlignment: index === 0 ? Text.AlignLeft : index === 4 ? Text.AlignRight : Text.AlignHCenter; required property int index } } }
                    }
                }
            }
            Heading { label: root.histogram.available ? qsTr("Mean %1 · Std dev %2 · %3 pixels").arg(Number(root.histogram.mean || 0).toFixed(1)).arg(Number(root.histogram.standardDeviation || 0).toFixed(1)).arg(Number(root.histogram.pixelCount || 0).toLocaleString()) : qsTr("Histogram requires a finite canvas") }
            Item { Layout.fillHeight: true }
            ActionGrid { Layout.fillWidth: true; entries: [{key: "shadow", label: qsTr("Shadow clipping"), icon: "Shadows", active: root.shadowClipping, enabled: Boolean(root.histogram.available)}, {key: "highlight", label: qsTr("Highlight clipping"), icon: "Highlights", active: root.highlightClipping, enabled: Boolean(root.histogram.available)}, {key: "refresh", label: qsTr("Refresh"), icon: "Generalrefresh", enabled: Boolean(root.histogram.available)}] }
        }
    }
    Component {
        id: informationView
        ColumnLayout {
            spacing: 12
            Heading { label: qsTr("Information") }
            Heading { label: root.selected.id ? root.selected.name + " · " + root.info.contentType : qsTr("Select a layer") }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Heading { label: qsTr("Layer details") }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Repeater {
                        model: [{label: qsTr("Dimensions"), value: root.info.dimensions}, {label: qsTr("Color profile"), value: root.info.profile}, {label: qsTr("Bit depth"), value: root.info.bitDepth}, {label: qsTr("Layer type"), value: root.info.contentType}, {label: qsTr("Origin"), value: root.info.origin}, {label: qsTr("Source"), value: root.info.source}]
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true; Layout.minimumHeight: 25; spacing: 8
                            Heading { Layout.leftMargin: 12; label: parent.modelData.label }
                            LV.Label { objectName: "layerInfo-" + parent.modelData.label; Layout.rightMargin: 12; Layout.preferredWidth: Math.min(180, root.width * 0.48); text: parent.modelData.value || "—"; style: caption; horizontalAlignment: Text.AlignRight; wrapMode: Text.Wrap; sizeToContentHeight: true }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Heading { label: qsTr("Color sample") }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: Math.max(56, sampleValues.implicitHeight + 16); color: LV.Theme.panelBackground08; radius: LV.Theme.radiusMd
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 8
                        Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 40; color: root.sample.color; radius: LV.Theme.radiusMd; border.color: LV.Theme.panelBackground12; border.width: root.sample.valid ? 0 : 1 }
                        ColumnLayout { id: sampleValues; Layout.fillWidth: true; spacing: 4; LV.Label { Layout.fillWidth: true; text: root.sample.hex; style: body } LV.Label { Layout.fillWidth: true; text: root.sample.rgb; style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true } }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            ActionGrid { Layout.fillWidth: true; preferredColumns: 4; entries: [{key: "sample", label: qsTr("Sample color"), icon: "Eyedropper", active: root.sample.picking, enabled: root.engine && root.engine.documentReady && !root.engine.infiniteCanvas}, {key: "copyColor", label: qsTr("Copy color"), icon: "DuplicateLayer", enabled: root.sample.valid}, {key: "copyDetails", label: qsTr("Copy details"), icon: "Metadata", enabled: Boolean(root.selected.id)}, {key: "reveal", label: qsTr("Reveal source"), icon: "Folder", enabled: Boolean(root.info.canReveal), reason: qsTr("Reveal the saved document containing the embedded media.")}] }
        }
    }
    LV.ContextMenu { id: blendMenu; showIconSlot: false; items: [{label: "Normal"}, {label: "Multiply"}, {label: "Screen"}, {label: "Overlay"}]; onItemTriggered: function(index) { root.report(root.engine.setLayerBlend(root.selected.id, items[index].label)) } }
    LV.ContextMenu { id: newMenu; showIconSlot: false; items: [{label: qsTr("Paint layer")}, {label: qsTr("Place image")}]; onItemTriggered: function(index) { if (index === 0) root.report(root.engine.addPaintLayer()); else root.toolState.actionRequested("file", "field-6", {}) } }
    LV.ContextMenu { id: adjustmentScopeMenu; showIconSlot: false; items: [{label: qsTr("Selected layer")}]; onItemTriggered: close() }
    LV.ContextMenu { id: sourceMenu; showIconSlot: false; items: [{label: qsTr("Composite")}, {label: qsTr("Selected layer"), enabled: Boolean(root.selected.id)}]; onItemTriggered: function(index) { root.composite = index === 0 } }
    LV.Sheet {
        id: adjustmentSheet
        objectName: "layerAdjustmentSheet"
        presentation: LV.Sheet.Mobile; contentPadding: 16; topSafeInset: 0; bottomSafeInset: 0
        contentComponent: Column {
            spacing: 12
            Repeater {
                model: root.adjustments.find(function(entry) { return entry.key === root.adjustmentKey })?.fields || []
                EditorToolControl {
                    required property string modelData
                    width: parent.width
                    field: modelData === "curveAmount" ? {id: "curveAmount", type: "Slider", label: qsTr("Curve amount"), initial: 0, minimum: -100, maximum: 100, step: 1, unit: "", decimals: 0} : Definitions.tool("color").fields.find(function(field) { return field.id === modelData })
                    value: root.adjustmentValues[modelData] === undefined ? field.initial : root.adjustmentValues[modelData]
                    onEdited: function(value) { root.editAdjustment(modelData, value) }
                    onRequested: { const success = root.engine.executeToolAction("color", modelData, root.adjustmentValues); root.report(success); if (success && modelData === "field-31") { adjustmentSheet.close(); if (root.toolState.visible) root.toolState.close() } }
                }
            }
            LV.LabelButton { objectName: "layerAdjustmentDone"; text: qsTr("Done"); onClicked: adjustmentSheet.close() }
        }
    }
}
