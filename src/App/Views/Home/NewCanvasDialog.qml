pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Controls.Popup {
    id: root
    objectName: "newCanvasDialog"
    parent: Controls.Overlay.overlay
    modal: true
    focus: true
    // LVRS ApplicationWindow's Figma Content uses 12px on every edge.
    padding: LV.Theme.gap12
    property Item availableArea: null
    readonly property rect availableBounds: {
        if (!availableArea || !parent)
            return Qt.rect(0, 0, parent ? parent.width : 1280, parent ? parent.height : 800)
        const geometry = Qt.rect(availableArea.x, availableArea.y, availableArea.width, availableArea.height)
        const origin = availableArea.mapToItem(parent, 0, 0)
        return Qt.rect(origin.x, origin.y, geometry.width, geometry.height)
    }
    closePolicy: Controls.Popup.CloseOnEscape
    width: Math.max(0, Math.min(1280, availableBounds.width - (compact ? 0 : LV.Theme.gap16 * 2)))
    height: Math.max(0, Math.min(880, availableBounds.height - (compact ? 0 : LV.Theme.gap16 * 2)))
    x: availableBounds.x + (availableBounds.width - width) / 2
    y: availableBounds.y + (availableBounds.height - height) / 2
    readonly property bool compact: availableBounds.width < 1000
    property int categoryIndex: 0
    property string query: ""
    property bool custom: false
    property var selectedPreset: ({})
    property string draftWidth: "1080"
    property string draftHeight: "1080"
    property string unit: "px"
    property string resolution: "300"
    property string canvasBackground: "White"
    property string creationError: ""
    readonly property Flickable galleryViewport: galleryScroll.contentItem as Flickable
    readonly property Flickable compactViewport: compactScroll.contentItem as Flickable
    readonly property Flickable categoryViewport: categoryScroll.contentItem as Flickable
    readonly property var sections: catalogue.sections(categoryIndex, query)
    readonly property int resultCount: {
        let total = 0
        for (const section of sections) total += section.presets.length
        return total
    }
    readonly property var specification: custom || selectedPreset.id
        ? catalogue.specification(Number(draftWidth), Number(draftHeight), unit, Number(resolution), canvasBackground)
        : ({valid: false, error: ""})
    readonly property string selectionName: custom ? qsTr("Custom canvas") : selectedPreset.name || qsTr("Choose a canvas")
    readonly property string dimensionText: draftWidth + " × " + draftHeight + " " + unit
    readonly property string aspectRatio: {
        if (!specification.valid) return "—"
        let a = specification.pixelWidth, b = specification.pixelHeight
        while (b) { const remainder = a % b; a = b; b = remainder }
        return specification.pixelWidth / a + ":" + specification.pixelHeight / a
    }
    signal canvasRequested(var specification)
    CanvasPresets { id: catalogue }

    function choose(preset) {
        selectedPreset = preset
        custom = false
        draftWidth = String(preset.width)
        draftHeight = String(preset.height)
        unit = preset.unit
        resolution = String(preset.ppi || 300)
        creationError = ""
    }
    function chooseCategory(index) {
        categoryIndex = index
        query = ""
        custom = false
        const groups = catalogue.sections(index, "")
        if (groups.length) choose(groups[0].presets[0])
    }
    function search(text) {
        query = text
        custom = false
        if (sections.length) choose(sections[0].presets[0])
        else selectedPreset = ({})
    }
    function customSize() {
        query = ""
        custom = true
        selectedPreset = ({})
        draftWidth = "1800"
        draftHeight = "1200"
        unit = "px"
        resolution = "300"
        creationError = ""
    }
    function begin() {
        canvasBackground = "White"
        creationError = ""
        chooseCategory(0)
        open()
    }
    function changeUnit(next) {
        const ppi = Number(resolution)
        const w = catalogue.convert(Number(draftWidth), unit, next, ppi)
        const h = catalogue.convert(Number(draftHeight), unit, next, ppi)
        custom = true
        unit = next
        draftWidth = String(next === "px" ? Math.round(w) : Math.round(w * 1000) / 1000)
        draftHeight = String(next === "px" ? Math.round(h) : Math.round(h * 1000) / 1000)
    }
    function create() {
        if (!specification.valid) return
        const value = Object.assign({}, specification, {
            name: selectionName, presetId: custom ? "" : selectedPreset.id,
            category: custom ? "Custom size" : selectedPreset.category,
            section: custom ? "Your dimensions" : selectedPreset.section
        })
        canvasRequested(value)
    }
    function scrollIntoView(control, viewport) {
        if (!viewport) return
        const point = control.mapToItem(viewport.contentItem, 0, 0)
        if (point.y < viewport.contentY)
            viewport.contentY = Math.max(0, point.y - 16)
        else if (point.y + control.height > viewport.contentY + viewport.height)
            viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height,
                point.y + control.height - viewport.height + 16))
    }
    background: Rectangle { color: "#202020"; radius: root.compact ? 0 : LV.Theme.radiusMd; border.color: LV.Theme.surfaceAlt }
    Controls.Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6) }
    Shortcut { sequence: StandardKey.Find; enabled: root.opened; onActivated: root.compact ? compactSearchField.forceInputFocus() : searchField.forceInputFocus() }
    Shortcut { sequence: "Ctrl+Return"; enabled: root.opened; onActivated: root.create() }

    contentItem: LV.VStack {
        alignment: Qt.AlignLeft
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? 132 : 96
            color: "#242424"
            LV.VStack {
                alignment: Qt.AlignLeft
                anchors.fill: parent
                anchors.margins: root.compact ? LV.Theme.gap12 : LV.Theme.gap24
                spacing: LV.Theme.gap8
                LV.HStack {
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap12
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: LV.Theme.gap2
                        LV.Label { Layout.fillWidth: true; text: qsTr("New canvas"); style: title2; elide: Text.ElideRight }
                        LV.Label { Layout.fillWidth: true; text: qsTr("Start with a size. Make it your own."); style: caption; visible: !root.compact }
                    }
                    LV.InputField {
                        id: searchField
                        objectName: "canvasPresetSearch"
                        Layout.preferredWidth: root.compact ? 0 : 368
                        visible: !root.compact
                        search: true
                        text: root.query
                        placeholderText: qsTr("Search sizes or formats")
                        onTextEdited: function(text) { root.search(text) }
                        // LVRS's clear affordance changes text without a textEdited event.
                        onTextChanged: if (text === "" && root.query !== "") root.search("")
                    }
                    LV.LabelButton {
                        objectName: "canvasCustomButton"
                        Layout.preferredWidth: root.compact ? 80 : 112
                        text: root.compact ? qsTr("Custom") : qsTr("Custom size")
                        tone: LV.AbstractButton.Default
                        onClicked: root.customSize()
                    }
                    LV.LabelButton {
                        objectName: "canvasCloseButton"
                        Layout.preferredWidth: root.compact ? 54 : 96
                        text: qsTr("Close")
                        tone: LV.AbstractButton.Default
                        onClicked: root.close()
                    }
                }
                LV.InputField {
                    id: compactSearchField
                    objectName: "canvasPresetSearchCompact"
                    Layout.fillWidth: true
                    visible: root.compact
                    search: true
                    text: root.query
                    placeholderText: qsTr("Search sizes or formats")
                    onTextEdited: function(text) { root.search(text) }
                    onTextChanged: if (text === "" && root.query !== "") root.search("")
                }
                LV.LabelMenuButton {
                    id: categoryButton
                    objectName: "canvasCategoryDropdown"
                    Layout.fillWidth: true
                    visible: root.compact
                    text: root.custom ? qsTr("Custom dimensions") : catalogue.categories[root.categoryIndex].name
                    tone: LV.AbstractButton.Default
                    onClicked: categoryMenu.openFor(categoryButton, 0, categoryButton.height + 2)
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            LV.HStack {
                anchors.fill: parent
                anchors.margins: LV.Theme.gap24
                spacing: LV.Theme.gap24
                visible: !root.compact
                Controls.ScrollView {
                    id: categoryScroll
                    Layout.preferredWidth: 200
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        width: 200
                        spacing: LV.Theme.gap2
                        LV.Label { text: qsTr("CATEGORIES"); style: caption; Layout.bottomMargin: LV.Theme.gap8 }
                        Repeater {
                            model: catalogue.categories
                            LV.MenuItem {
                                id: categoryItem
                                required property int index
                                required property var modelData
                                objectName: "canvasCategory_" + index
                                Layout.fillWidth: true
                                label: modelData.name
                                iconName: "imagefitContent"
                                keyVisible: false
                                showChevron: false
                                backgroundColor: isSelected ? "#283653" : "transparent"
                                backgroundColorHover: isSelected ? "#283653" : LV.Theme.surfaceAlt
                                backgroundColorPressed: isSelected ? "#283653" : LV.Theme.accentMuted
                                state: !root.custom && root.categoryIndex === index && root.query === "" ? selectedState : defaultState
                                onClicked: root.chooseCategory(index)
                                onActiveFocusChanged: if (activeFocus) root.scrollIntoView(categoryItem, root.categoryViewport)
                            }
                        }
                        LV.Label { text: qsTr("%1 canvas presets").arg(catalogue.count); style: caption; Layout.topMargin: LV.Theme.gap8 }
                        LV.LabelButton {
                            Layout.fillWidth: true
                            text: qsTr("Custom dimensions")
                            tone: root.custom ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                            onClicked: root.customSize()
                        }
                    }
                }
                Controls.ScrollView {
                    id: galleryScroll
                    objectName: "canvasGalleryScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    Loader { width: galleryScroll.availableWidth; sourceComponent: root.visible && !root.compact ? galleryComponent : null }
                }
                Controls.ScrollView {
                    id: detailsScroll
                    objectName: "canvasDetailsScroll"
                    Layout.preferredWidth: 268
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    background: Rectangle { color: "#242424" }
                    Loader { width: detailsScroll.availableWidth; sourceComponent: root.visible && !root.compact ? detailsComponent : null }
                }
            }
            Controls.ScrollView {
                id: compactScroll
                anchors.fill: parent
                anchors.margins: LV.Theme.gap12
                visible: root.compact
                contentWidth: availableWidth
                clip: true
                LV.VStack {
                    alignment: Qt.AlignLeft
                    width: compactScroll.availableWidth
                    spacing: LV.Theme.gap16
                    Loader { Layout.fillWidth: true; sourceComponent: root.visible && root.compact ? galleryComponent : null }
                    Loader { Layout.fillWidth: true; sourceComponent: root.visible && root.compact ? detailsComponent : null }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? 94 : 68
            color: "#242424"
            LV.HStack {
                anchors.fill: parent
                anchors.margins: root.compact ? LV.Theme.gap12 : LV.Theme.gap24
                spacing: LV.Theme.gap12
                LV.VStack {
                    alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: LV.Theme.gap2
                    LV.Label {
                        objectName: "canvasSelectionSummary"
                        Layout.fillWidth: true
                        text: root.specification.valid ? root.selectionName + " · " + root.dimensionText : qsTr("Choose a canvas size")
                        style: description
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    LV.Label { Layout.fillWidth: true; text: qsTr("Blank canvas · %1 background").arg(root.canvasBackground.toLowerCase()); style: caption; wrapMode: Text.Wrap }
                }
                LV.LabelButton { objectName: "canvasCancelButton"; Layout.preferredWidth: root.compact ? 64 : 88; text: qsTr("Cancel"); tone: LV.AbstractButton.Default; onClicked: root.close() }
                LV.LabelButton {
                    objectName: "canvasCreateButton"
                    Layout.preferredWidth: root.compact ? 96 : 112
                    text: qsTr("Create canvas")
                    tone: LV.AbstractButton.Primary
                    enabled: root.specification.valid
                    onClicked: root.create()
                }
            }
        }
    }

    Component {
        id: galleryComponent
        LV.VStack {
            alignment: Qt.AlignLeft
            id: gallery
            spacing: LV.Theme.gap16
            LV.HStack {
                Layout.fillWidth: true
                LV.Label {
                    Layout.fillWidth: true
                    text: root.custom ? qsTr("Custom dimensions") : root.query !== "" ? qsTr("Search results") : catalogue.categories[root.categoryIndex].name
                    style: header
                    wrapMode: Text.Wrap
                }
                LV.Label { text: qsTr("%1 sizes").arg(root.resultCount); style: caption; visible: !root.custom }
            }
            LV.Label {
                Layout.fillWidth: true
                text: root.custom ? qsTr("A blank canvas with your exact dimensions.")
                    : root.query !== "" ? qsTr("Matching formats across all categories.") : catalogue.categories[root.categoryIndex].subtitle
                style: description
                wrapMode: Text.Wrap
            }
            CanvasSizePreview {
                Layout.fillWidth: true
                Layout.preferredHeight: root.compact ? 180 : 320
                visible: root.custom
                pixelWidth: root.specification.pixelWidth || 1
                pixelHeight: root.specification.pixelHeight || 1
                selected: true
                showRatio: true
                ratioLabel: root.aspectRatio
            }
            LV.Label { text: qsTr("Use px for digital graphics, mm or inches for print."); style: caption; visible: root.custom; Layout.fillWidth: true; wrapMode: Text.Wrap }
            GridLayout {
                Layout.fillWidth: true
                visible: root.custom
                columns: root.compact ? 1 : 3
                rowSpacing: LV.Theme.gap8
                columnSpacing: LV.Theme.gap12
                Repeater {
                    model: [{name: "Square · 1:1", w:1080,h:1080}, {name:"Landscape · 16:9",w:1920,h:1080}, {name:"Portrait · 9:16",w:1080,h:1920}]
                    LV.LabelButton {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.name
                        tone: LV.AbstractButton.Default
                        onClicked: { root.unit = "px"; root.draftWidth = String(modelData.w); root.draftHeight = String(modelData.h) }
                    }
                }
            }
            LV.VStack {
                alignment: Qt.AlignLeft
                Layout.fillWidth: true
                Layout.topMargin: LV.Theme.gap24
                visible: !root.custom && root.resultCount === 0
                spacing: LV.Theme.gap12
                LV.Label { text: qsTr("No formats found"); style: header }
                LV.Label { Layout.fillWidth: true; text: qsTr("Try a platform, format or size, such as Instagram, A4 or 1920 × 1080."); style: description; wrapMode: Text.Wrap }
                LV.LabelButton { objectName: "canvasClearSearchButton"; text: qsTr("Clear search"); tone: LV.AbstractButton.Default; onClicked: root.search("") }
            }
            Repeater {
                model: root.custom ? [] : root.sections
                LV.VStack {
                    alignment: Qt.AlignLeft
                    id: section
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap12
                    LV.HStack {
                        Layout.fillWidth: true
                        LV.Label { Layout.fillWidth: true; text: section.modelData.name; style: header2; wrapMode: Text.Wrap }
                        LV.Label { text: qsTr("%1 formats").arg(section.modelData.presets.length); style: caption }
                    }
                    GridLayout {
                        id: presetGrid
                        Layout.fillWidth: true
                        columns: Math.max(1, Math.min(3, Math.floor((gallery.width + 12) / 220)))
                        columnSpacing: LV.Theme.gap12
                        rowSpacing: LV.Theme.gap12
                        Repeater {
                            model: section.modelData.presets
                            Rectangle {
                                id: card
                                required property var modelData
                                objectName: "canvasPresetCard_" + modelData.id
                                Layout.fillWidth: true
                                Layout.preferredWidth: (gallery.width - (presetGrid.columns - 1) * 12) / presetGrid.columns
                                Layout.preferredHeight: 144
                                radius: LV.Theme.radiusMd
                                color: "#242424"
                                border.width: root.selectedPreset.id === modelData.id && !root.custom ? 1 : 0
                                border.color: LV.Theme.accent
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.name
                                Accessible.description: modelData.width + " × " + modelData.height + " " + modelData.unit
                                Accessible.onPressAction: card.selectPreset()
                                function selectPreset() { card.forceActiveFocus(); root.choose(card.modelData) }
                                function revealFocus() {
                                    const viewport = root.compact ? root.compactViewport : root.galleryViewport
                                    if (!card.activeFocus || !viewport || viewport.height <= 0) return
                                    root.scrollIntoView(card.height <= viewport.height ? card : presetDetails, viewport)
                                }
                                Keys.onSpacePressed: card.selectPreset()
                                Keys.onReturnPressed: card.selectPreset()
                                Keys.onEnterPressed: card.selectPreset()
                                onActiveFocusChanged: if (activeFocus) Qt.callLater(card.revealFocus)
                                Connections {
                                    target: root.compact ? root.compactViewport : root.galleryViewport
                                    // A newly opened popup's viewport is laid out after its cards.
                                    function onHeightChanged() { if (card.activeFocus) Qt.callLater(card.revealFocus) }
                                    function onContentHeightChanged() { if (card.activeFocus) Qt.callLater(card.revealFocus) }
                                }
                                TapHandler { onTapped: card.selectPreset() }
                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    radius: card.radius
                                    border.width: card.activeFocus ? 2 : 0
                                    border.color: LV.Theme.accent
                                }
                                LV.VStack {
                                    alignment: Qt.AlignLeft
                                    anchors.fill: parent
                                    anchors.margins: LV.Theme.gap12
                                    spacing: LV.Theme.gap8
                                    CanvasSizePreview {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 62
                                        pixelWidth: card.modelData.pixelWidth
                                        pixelHeight: card.modelData.pixelHeight
                                        selected: root.selectedPreset.id === card.modelData.id
                                    }
                                    Item {
                                        id: presetDetails
                                        objectName: "canvasPreset_" + card.modelData.id
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 44
                                        clip: true
                                        // Detached ListItem.Navigation appearance; selection belongs to the card.
                                        LV.VStack {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.leftMargin: LV.Theme.gap12
                                            anchors.rightMargin: LV.Theme.gap12
                                            anchors.verticalCenter: parent.verticalCenter
                                            height: implicitHeight
                                            spacing: LV.Theme.gap4
                                            alignment: Qt.AlignLeft
                                            LV.Label {
                                                objectName: "canvasPresetLabel"
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                Layout.preferredHeight: LV.Theme.textBodyLineHeight
                                                style: body
                                                text: card.modelData.name
                                                elide: Text.ElideRight
                                            }
                                            LV.Label {
                                                objectName: "canvasPresetDescription"
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                Layout.preferredHeight: LV.Theme.textCaptionLineHeight
                                                style: caption
                                                text: card.modelData.width + " × " + card.modelData.height + " " + card.modelData.unit
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: detailsComponent
        Rectangle {
            id: details
            objectName: "canvasDetails"
            color: "#242424"
            implicitHeight: detailsColumn.implicitHeight + 32
            LV.VStack {
                alignment: Qt.AlignLeft
                id: detailsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: LV.Theme.gap16
                spacing: LV.Theme.gap12
                LV.Label { text: qsTr("CANVAS DETAILS"); style: caption }
                CanvasSizePreview {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 124
                    pixelWidth: root.specification.pixelWidth || 1
                    pixelHeight: root.specification.pixelHeight || 1
                    selected: root.specification.valid
                    showRatio: root.specification.valid && root.unit === "px"
                    ratioLabel: root.aspectRatio
                }
                LV.Label { Layout.fillWidth: true; text: root.selectionName; style: header2; wrapMode: Text.Wrap }
                LV.Label {
                    Layout.fillWidth: true
                    text: root.custom ? qsTr("Custom size · Your dimensions")
                        : root.selectedPreset.id ? root.selectedPreset.category + " · " + root.selectedPreset.section
                        : qsTr("Select a format or set custom dimensions.")
                    style: caption
                    wrapMode: Text.Wrap
                }
                LV.HStack {
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap12
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        spacing: LV.Theme.gap4
                        LV.Label { text: qsTr("Width"); style: caption }
                        LV.InputField {
                            objectName: "canvasWidthInput"
                            Layout.fillWidth: true
                            text: root.draftWidth
                            clearButtonVisible: false
                            enabled: root.custom || !!root.selectedPreset.id
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: function(text) { root.custom = true; root.draftWidth = text }
                        }
                    }
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        spacing: LV.Theme.gap4
                        LV.Label { text: qsTr("Height"); style: caption }
                        LV.InputField {
                            objectName: "canvasHeightInput"
                            Layout.fillWidth: true
                            text: root.draftHeight
                            clearButtonVisible: false
                            enabled: root.custom || !!root.selectedPreset.id
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: function(text) { root.custom = true; root.draftHeight = text }
                        }
                    }
                }
                LV.VStack {
                    alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap4
                    LV.Label { text: qsTr("Units"); style: caption }
                    LV.LabelMenuButton {
                        id: unitsButton
                        objectName: "canvasUnitsButton"
                        Layout.fillWidth: true
                        text: root.unit === "px" ? qsTr("Pixels (px)") : root.unit === "mm" ? qsTr("Millimeters (mm)") : qsTr("Inches (in)")
                        tone: LV.AbstractButton.Default
                        onClicked: unitsMenu.openFor(unitsButton, 0, unitsButton.height + 2)
                    }
                }
                LV.VStack {
                    alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    visible: root.unit !== "px"
                    spacing: LV.Theme.gap4
                    LV.Label { text: qsTr("Print resolution (PPI)"); style: caption }
                    LV.InputField { objectName: "canvasPpiInput"; Layout.fillWidth: true; text: root.resolution; clearButtonVisible: false; inputMethodHints: Qt.ImhDigitsOnly; onTextEdited: function(text) { root.custom = true; root.resolution = text } }
                    LV.Label { Layout.fillWidth: true; text: root.specification.valid ? root.specification.pixelWidth + " × " + root.specification.pixelHeight + " px" : "—"; style: caption }
                }
                LV.Label { Layout.fillWidth: true; visible: root.unit === "px"; text: qsTr("Aspect ratio: %1").arg(root.aspectRatio); style: caption; wrapMode: Text.WrapAnywhere }
                LV.VStack {
                    alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap4
                    LV.Label { text: qsTr("Background"); style: caption }
                    LV.LabelMenuButton {
                        id: backgroundButton
                        objectName: "canvasBackgroundButton"
                        Layout.fillWidth: true
                        text: root.canvasBackground
                        tone: LV.AbstractButton.Default
                        onClicked: backgroundMenu.openFor(backgroundButton, 0, backgroundButton.height + 2)
                    }
                }
                LV.Label {
                    objectName: "canvasValidationError"
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: root.creationError || root.specification.error || ""
                    style: caption
                    color: LV.Theme.danger
                    wrapMode: Text.Wrap
                }
                LV.Label { Layout.fillWidth: true; text: qsTr("Select a preset or customize its dimensions before creating."); style: caption; wrapMode: Text.Wrap }
            }
        }
    }
    LV.ContextMenu {
        id: categoryMenu
        items: catalogue.categories.map(function(category) { return {label: category.name} })
        onItemTriggered: function(index, item) { root.chooseCategory(index) }
    }
    LV.ContextMenu {
        id: unitsMenu
        items: [{label:"Pixels (px)", unit:"px"}, {label:"Millimeters (mm)",unit:"mm"}, {label:"Inches (in)",unit:"in"}]
        onItemTriggered: function(index, item) { root.changeUnit(item.unit) }
    }
    LV.ContextMenu {
        id: backgroundMenu
        items: [{label:"White"}, {label:"Transparent"}, {label:"Black"}]
        onItemTriggered: function(index, item) { root.canvasBackground = item.label }
    }
}
