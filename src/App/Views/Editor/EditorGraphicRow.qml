pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV

Item {
    id: root
    required property var entry
    required property var presenter
    objectName: "graphicControl-" + entry.id
    implicitHeight: rowContent.item ? rowContent.item.implicitHeight : (entry.height || 22)
    height: implicitHeight
    readonly property bool editable: entry.enabled !== false && entry.supported !== false && !presenter.busy
    function currentValue() { return presenter.value(entry) }
    function numericText() { return NativeData.graphicState.numberText(entry, currentValue()) }
    function click(option) { presenter.activate(option, entry) }
    Loader {
        id: rowContent
        anchors.fill: parent
        sourceComponent: root.entry.type === "section" && String(root.entry.id).endsWith("/swatch") ? swatchComponent
            : root.entry.type === "section" ? sectionComponent
            : root.entry.type === "gallery" ? galleryComponent
            : root.entry.type === "choices" ? choicesComponent
            : root.entry.type === "action" ? actionComponent
            : root.entry.type === "curve" ? curveComponent
            : root.entry.type === "field" ? fieldComponent : noteComponent
    }
    Component {
        id: fieldComponent
        Item {
            implicitHeight: root.entry.control === "slider" ? 63 : (root.entry.height || 22)
            LV.Label {
                x: 0; y: 4.5
                width: parent.width - valueInput.width - 8
                height: 13
                style: body
                text: root.entry.label
                elide: Text.ElideRight
            }
            LV.InputField {
                id: valueInput
                visible: root.entry.control !== "toggle" && root.entry.control !== "select"
                objectName: "graphicInput-" + root.entry.id
                x: parent.width - width
                width: Math.min(130, parent.width * 0.52)
                height: 22
                fieldMinHeight: 22
                clearButtonVisible: false
                property bool textModified: false
                enabled: root.editable
                text: root.entry.control === "text" ? String(root.currentValue() || "") : root.numericText()
                Accessible.name: root.entry.label
                function commit() {
                    textModified = false
                    const next = root.entry.control === "text" ? text : NativeData.graphicState.parseNumber(root.entry, text)
                    if (next !== undefined) { root.presenter.edit(root.entry, next); root.presenter.commitField(root.entry) }
                    text = Qt.binding(function() { return root.entry.control === "text" ? String(root.currentValue() || "") : root.numericText() })
                }
                onAccepted: commit()
                onTextEdited: textModified = true
                onActiveFocusChanged: if (!activeFocus && textModified) commit()
            }
            LV.ToggleSwitch {
                visible: root.entry.control === "toggle"
                objectName: "graphicToggle-" + root.entry.id
                x: parent.width - width
                width: 38; height: 22
                trackWidth: 38
                enabled: root.editable
                checked: Boolean(root.currentValue())
                Accessible.name: root.entry.label
                onToggled: root.presenter.edit(root.entry, checked)
            }
            LV.LabelButton {
                visible: root.entry.control === "select"
                objectName: "graphicSelect-" + root.entry.id
                x: parent.width - width
                width: Math.min(130, parent.width * 0.52); height: 22
                text: String(root.currentValue() || "")
                enabled: root.editable
                Accessible.name: root.entry.label
                onClicked: {
                    const next = NativeData.presentation.nextChoice(root.entry.options || [], root.currentValue())
                    if (next !== undefined && next !== null) root.click(next)
                }
            }
            LV.Slider {
                visible: root.entry.control === "slider"
                objectName: "graphicSlider-" + root.entry.id
                x: 0; y: 26
                width: parent.width; height: 22
                size: LV.Slider.Mini
                showSymbol: false
                showLabels: false
                motionEnabled: false
                from: root.entry.min === undefined ? 0 : root.entry.min
                to: root.entry.max === undefined ? 100 : root.entry.max
                stepSize: root.entry.step || 0.01
                value: root.currentValue() === null ? from : Number(root.currentValue() || 0)
                enabled: root.editable
                Accessible.name: root.entry.label
                property bool sourceMoved: false
                onMoved: { root.presenter.edit(root.entry, value); sourceMoved = true }
                onPressedChanged: if (!pressed && sourceMoved) { sourceMoved = false; root.presenter.commitField(root.entry) }
            }
            LV.Label {
                visible: root.entry.control === "slider"
                x: 0; y: 52
                width: parent.width; height: 11
                style: caption
                text: root.entry.rangeText || (root.entry.min + " … " + root.entry.max + (root.entry.unit ? " " + root.entry.unit : ""))
                elide: Text.ElideRight
            }
        }
    }
    Component {
        id: swatchComponent
        Rectangle {
            implicitHeight: root.entry.height || 32
            radius: 8
            color: root.presenter.swatchColor()
            Accessible.name: root.entry.label
        }
    }
    Component {
        id: sectionComponent
        Column {
            spacing: root.entry.gap === undefined ? 8 : root.entry.gap
            LV.Label { width: parent.width; height: (root.entry.rows || []).length ? 13 : (root.entry.height || 13); text: root.entry.label; style: root.entry.style === "caption" ? caption : body; elide: root.entry.style === "caption" ? Text.ElideNone : Text.ElideRight; wrapMode: root.entry.style === "caption" ? Text.Wrap : Text.NoWrap }
            Repeater {
                model: root.entry.rows || []
                Loader {
                    required property var modelData
                    width: root.width
                    height: item ? item.implicitHeight : 0
                    Component.onCompleted: setSource(Qt.resolvedUrl("EditorGraphicRow.qml"), {entry: modelData, presenter: root.presenter})
                }
            }
        }
    }
    Component {
        id: choicesComponent
        Column {
            id: choiceGroup
            spacing: 8
            Item {
                width: parent.width; height: 13
                LV.Label { width: parent.width - (root.entry.editorTarget ? 72 : 0); height: 13; text: root.entry.label; style: body; elide: Text.ElideRight }
                LV.LabelButton { visible: Boolean(root.entry.editorTarget); objectName: "graphicNavigate-" + root.entry.editorTarget; x: parent.width - 64; y: -4.5; width: 64; height: 22; text: qsTr("Edit"); onClicked: root.presenter.selectView(root.entry.editorTarget) }
            }
            Item {
                id: grid
                width: parent.width
                readonly property int columns: root.entry.columns || 2
                readonly property var options: root.entry.options || []
                height: Math.ceil(options.length / columns) * 22 + Math.max(0, Math.ceil(options.length / columns) - 1) * 8
                Repeater {
                    model: grid.options
                    LV.LabelButton {
                        id: selectionButton1
                        required property var modelData
                        required property int index
                        readonly property var option: typeof modelData === "object" ? modelData : {label: String(modelData), value: modelData}
                        readonly property int rowColumns: Math.min(grid.columns, grid.options.length - Math.floor(index / grid.columns) * grid.columns)
                        objectName: option.target ? "graphicNavigate-" + option.target : "graphicChoice-" + root.entry.id + "-" + index
                        x: (index % grid.columns) * (width + 8)
                        y: Math.floor(index / grid.columns) * 30
                        width: (grid.width - (rowColumns - 1) * 8) / rowColumns
                        height: 22
                        text: option.label
                        tone: LV.AbstractButton.Borderless
                        enabled: root.editable && option.enabled !== false
                        Accessible.name: root.entry.label + ": " + option.label
                        Accessible.checkable: true
                        Accessible.checked: root.currentValue() === option.value || option.selected === true
                        onClicked: root.click(option)

                        EditorButtonState { button: selectionButton1; selected: Boolean((option.selected || root.currentValue() === option.value)) }
                        Accessible.onToggleAction: selectionButton1.click()
                    }
                }
            }
        }
    }
    Component {
        id: galleryComponent
        Item {
            id: gallery
            readonly property var options: root.entry.options || []
            readonly property int columns: Math.min(3, Math.max(1, Math.floor((width + 8) / 95)))
            implicitHeight: Math.ceil(options.length / columns) * 51 + Math.max(0, Math.ceil(options.length / columns) - 1) * 8
            Repeater {
                model: gallery.options
                Item {
                    id: tile
                    required property var modelData
                    required property int index
                    readonly property int rowColumns: Math.min(gallery.columns, gallery.options.length - Math.floor(index / gallery.columns) * gallery.columns)
                    width: (gallery.width - (rowColumns - 1) * 8) / rowColumns
                    height: 51
                    x: (index % gallery.columns) * (width + 8)
                    y: Math.floor(index / gallery.columns) * 59
                    LV.IconButton {
                        id: selectionButton2
                        objectName: "graphicNavigate-" + tile.modelData.target
                        x: (parent.width - width) / 2
                        width: 36; height: 30
                        horizontalPadding: 2; verticalPadding: 2
                        iconSource: root.presenter.asset(tile.modelData.icon || "adjust")
                        iconSize: 18
                        tone: LV.AbstractButton.Borderless
                        Accessible.name: tile.modelData.label
                        onClicked: root.click(tile.modelData)

                        Accessible.checkable: selectionButton2.checkable
                        Accessible.checked: selectionButton2.checked
                        EditorButtonState { button: selectionButton2; selected: Boolean(tile.modelData.selected) }
                        Accessible.onToggleAction: selectionButton2.click()
                    }
                    LV.Label {
                        y: 38
                        width: parent.width; height: 13
                        text: tile.modelData.label
                        style: body
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
    Component {
        id: actionComponent
        Item {
            implicitHeight: root.entry.height || 22
            LV.Label {
                visible: Boolean(root.entry.label)
                x: 0; y: 4.5
                width: parent.width - actionButton.width - 8
                height: 13
                text: root.entry.label
                style: body
                elide: Text.ElideRight
            }
            LV.LabelButton {
                id: actionButton
                tone: LV.AbstractButton.Default
                visible: Boolean(root.entry.label)
                objectName: root.entry.target ? "graphicNavigate-" + root.entry.target : "graphicAction-" + root.entry.id
                x: parent.width - width
                width: Math.min(82, parent.width * 0.4); height: 22
                text: root.entry.buttonLabel || (root.entry.target || root.entry.editor ? qsTr("Edit") : root.entry.options && root.entry.options.length ? root.entry.options[0].label : root.entry.label)
                enabled: root.editable
                Accessible.name: root.entry.label
                onClicked: root.click(root.entry.options && root.entry.options.length ? root.entry.options[0] : root.entry)
            }
            Row {
                visible: !root.entry.label
                width: parent.width
                height: 22
                spacing: 8
                Repeater {
                    model: root.entry.options || []
                    LV.LabelButton {
                        required property var modelData
                        required property int index
                        objectName: modelData.target && NativeData.presentation.indexOf(["preview", "apply", "overview"], modelData.value) < 0 ? "graphicNavigate-" + modelData.target : "graphicAction-" + root.entry.id + "-" + index
                        width: (root.width - ((root.entry.options || []).length - 1) * 8) / (root.entry.options || []).length
                        height: 22
                        tone: LV.AbstractButton.Default
                        text: modelData.label
                        enabled: root.editable && modelData.enabled !== false
                        Accessible.name: modelData.label
                        onClicked: root.click(modelData)
                    }
                }
            }
        }
    }
    Component {
        id: curveComponent
        Rectangle {
            implicitHeight: root.entry.height || 160
            color: LV.Theme.panelBackground08
            radius: 8
            border.color: LV.Theme.panelBackground12
            Canvas {
                id: curveCanvas
                anchors.fill: parent
                anchors.margins: 10
                property var draft: root.presenter.toolState.values.graphicDrafts
                onDraftChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = LV.Theme.panelBackground12
                    ctx.lineWidth = 1
                    for (let i = 1; i < 4; ++i) { ctx.beginPath(); ctx.moveTo(width * i / 4, 0); ctx.lineTo(width * i / 4, height); ctx.moveTo(0, height * i / 4); ctx.lineTo(width, height * i / 4); ctx.stroke() }
                    ctx.beginPath(); ctx.strokeStyle = LV.Theme.captionColor; ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.stroke()
                    const points = root.presenter.curvePoints(root.entry)
                    const channel = String(root.presenter.curveChannel() || "")
                    ctx.strokeStyle = channel === "Red" ? "#ff453a" : channel === "Green" ? "#30d158" : channel === "Blue" ? "#0a84ff" : LV.Theme.bodyColor
                    ctx.lineWidth = 2; ctx.beginPath()
                    if (Array.isArray(points)) points.forEach(function(p, i) { const x = (Array.isArray(p) ? p[0] : p.input === undefined ? p.x : p.input) * curveCanvas.width; const y = (1 - (Array.isArray(p) ? p[1] : p.output === undefined ? p.y : p.output)) * curveCanvas.height; if (!i) ctx.moveTo(x, y); else ctx.lineTo(x, y) })
                    ctx.stroke()
                }
            }
        }
    }
    Component {
        id: noteComponent
        LV.Label {
            width: root.width
            implicitHeight: Math.max(root.entry.height || 13, contentHeight)
            text: root.entry.label || root.entry.text || ""
            style: caption
            wrapMode: Text.Wrap
        }
    }
}
