pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

// A passive panel surface. Only its value controls accept input or show hover.
Item {
    id: root
    property int type: LV.ListItem.Mini
    property string label: ""
    property string description: ""
    property string value: ""
    property string inputText1: ""
    property bool checked: false
    property int selectorIndex: 0
    property var selector: ({})
    property var input1: ({})
    property var primaryAction: ({})
    property var secondaryAction: ({})
    property bool showLeadingIcon: true
    property bool showDescription: true
    property bool showValue: true
    property bool showTrailingIcon: true
    property bool showPrimaryAction: true
    property bool showSecondaryAction: true
    property bool showMoreMenu: true
    property bool navigationEnabled: showValue || showTrailingIcon
    property string iconName: "nodesfolder"
    property url iconSource: ""
    property string trailingIconName: "generalchevronRight"
    property int iconSize: LV.Theme.iconSm
    property int inputWidth: LV.Theme.scaleMetric(140)
    property int navigationItemWidth: LV.Theme.scaleMetric(280)
    property int miniItemWidth: LV.Theme.scaleMetric(170)
    property int minItemWidth: type === LV.ListItem.Mini ? miniItemWidth
        : type === LV.ListItem.Navigation || type === LV.ListItem.Toggle ? navigationItemWidth : LV.Theme.scaleMetric(400)
    property int standardItemHeight: LV.Theme.scaleMetric(44)
    property Component trailingComponent: null
    readonly property alias inputControl: inputField
    readonly property bool flatPanelRow: true
    readonly property bool effectiveEnabled: enabled
    readonly property bool mini: type === LV.ListItem.Mini
    readonly property string variantName: ["Mini", "Detail", "Navigation", "Toggle", "Checkable", "Action", "ActionGroup", "Stepper", "Select", "InlineEdit"][type] || "Mini"
    readonly property int insetHorizontal: mini ? LV.Theme.gap4 : LV.Theme.scaleMetric(12)
    readonly property int insetVertical: mini ? LV.Theme.gap2 : LV.Theme.gap8
    signal edited(string field, var value)
    signal actionTriggered(string action, var payload)
    signal clicked()
    activeFocusOnTab: false
    clip: !mini
    Accessible.role: Accessible.Grouping
    Accessible.name: label
    Accessible.description: description
    implicitWidth: Math.max(minItemWidth, content.implicitWidth + 2 * insetHorizontal)
    implicitHeight: mini ? iconSize + 2 * insetVertical : Math.max(standardItemHeight, content.implicitHeight + 2 * insetVertical)

    function configValue(config, name, fallback) { return config && config[name] !== undefined && config[name] !== null ? config[name] : fallback }
    function optionAt(options, index) { return !options || index < 0 ? null : typeof options.get === "function" ? options.get(index) : options[index] }
    function optionText(option, fallback) { return option === undefined || option === null ? fallback : typeof option === "object" ? String(configValue(option, "text", configValue(option, "label", fallback))) : String(option) }
    function editValue(field, next) {
        if (!enabled || ["checked", "selectorIndex", "inputText1"].indexOf(field) < 0 || root[field] === next) return false
        root[field] = next
        edited(field, next)
        return true
    }
    function triggerAction(action) {
        const config = action === "secondary" ? secondaryAction : primaryAction
        if (!enabled || configValue(config, "enabled", true) === false || configValue(config, "tone", LV.AbstractButton.Default) === LV.AbstractButton.Disabled) return false
        const values = {inputText1: inputText1, checked: checked, selectorIndex: selectorIndex}
        const method = configValue(config, "method", configValue(config, "onTriggered", null))
        if (method) method({source: root, action: action, values: values})
        actionTriggered(action, values)
        return true
    }

    component ActionButton: LV.PushButton {
        required property string actionName
        readonly property var config: actionName === "secondary" ? root.secondaryAction : root.primaryAction
        objectName: "panelRow_" + actionName + "Action"
        implicitWidth: LV.Theme.scaleMetric(72)
        text: root.configValue(config, "text", root.type === LV.ListItem.InlineEdit ? qsTr("Save") : qsTr("Open"))
        tone: root.configValue(config, "tone", actionName === "primary" ? LV.AbstractButton.Primary : LV.AbstractButton.Default)
        enabled: root.configValue(config, "enabled", true)
        Accessible.name: root.label + ": " + text
        onClicked: root.triggerAction(actionName)
    }

    RowLayout {
        id: content
        x: root.insetHorizontal
        y: (root.height - height) / 2
        width: Math.max(0, root.width - 2 * root.insetHorizontal)
        height: implicitHeight
        spacing: root.mini ? LV.Theme.scaleMetric(1) : LV.Theme.gap8
        Image {
            visible: root.showLeadingIcon
            Layout.preferredWidth: root.iconSize; Layout.preferredHeight: root.iconSize
            source: root.iconSource.toString().length ? root.iconSource : LV.Theme.iconPath(root.iconName)
            sourceSize: Qt.size(root.iconSize * 4, root.iconSize * 4)
            fillMode: Image.PreserveAspectFit
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            spacing: LV.Theme.gap4
            LV.Label {
                objectName: "panelRow_label"
                text: root.label; style: body
                color: root.enabled ? LV.Theme.bodyColor : LV.Theme.disabledColor
                Layout.fillWidth: true; Layout.minimumWidth: 0
                Layout.preferredHeight: LV.Theme.textBodyLineHeight; Layout.maximumHeight: LV.Theme.textBodyLineHeight
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            LV.Label {
                visible: !root.mini && root.showDescription
                text: root.description; style: caption
                color: root.enabled ? LV.Theme.captionColor : LV.Theme.disabledColor
                Layout.fillWidth: true; Layout.minimumWidth: 0
                Layout.preferredHeight: LV.Theme.textCaptionLineHeight; Layout.maximumHeight: LV.Theme.textCaptionLineHeight
                verticalAlignment: Text.AlignTop; elide: Text.ElideRight
            }
        }
        Loader {
            visible: root.trailingComponent !== null
            active: visible; sourceComponent: root.trailingComponent
            Layout.preferredWidth: implicitWidth; Layout.preferredHeight: implicitHeight
        }
        LV.PushButton {
            objectName: "panelRow_navigation"
            visible: root.type === LV.ListItem.Navigation && root.trailingComponent === null && (root.showValue || root.showTrailingIcon)
            enabled: root.navigationEnabled
            tone: LV.AbstractButton.Borderless
            horizontalPadding: 0; verticalPadding: 0
            implicitWidth: (root.showValue ? LV.Theme.scaleMetric(72) : 0) + (root.showTrailingIcon ? root.iconSize : 0) + (root.showValue && root.showTrailingIcon ? LV.Theme.gap4 : 0)
            implicitHeight: root.iconSize
            Accessible.name: root.label + ": " + (root.value || qsTr("Open"))
            onClicked: root.clicked()
            contentItem: LV.HStack {
                spacing: LV.Theme.gap4
                LV.Label {
                    visible: root.showValue
                    text: root.value; style: caption; color: LV.Theme.captionColor
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: LV.Theme.scaleMetric(72)
                    Layout.preferredHeight: LV.Theme.textCaptionLineHeight
                }
                Image {
                    visible: root.showTrailingIcon
                    Layout.preferredWidth: root.iconSize; Layout.preferredHeight: root.iconSize
                    source: LV.Theme.iconPath(root.trailingIconName)
                    sourceSize: Qt.size(root.iconSize * 4, root.iconSize * 4)
                    fillMode: Image.PreserveAspectFit
                }
            }
        }
        LV.ToggleSwitch {
            objectName: "panelRow_toggle"
            visible: root.type === LV.ListItem.Toggle
            checked: root.checked
            Accessible.name: root.label
            onToggled: root.editValue("checked", checked)
        }
        Loader {
            visible: root.type === LV.ListItem.Select
            active: visible
            sourceComponent: LV.ListItemSelector { objectName: "panelRow_selector"; listItem: root; config: root.selector }
            Layout.preferredWidth: implicitWidth; Layout.preferredHeight: implicitHeight
        }
        LV.InputField {
            id: inputField
            objectName: "panelRow_input1"
            visible: root.type === LV.ListItem.InlineEdit
            Layout.preferredWidth: root.inputWidth
            text: root.inputText1
            clearButtonVisible: root.configValue(root.input1, "clearButtonVisible", true)
            Accessible.name: root.label
            onTextEdited: function(text) { root.editValue("inputText1", text) }
            onAccepted: function(text) { root.actionTriggered("inputSubmitted", {field: "inputText1", text: text}) }
        }
        ActionButton {
            actionName: "primary"
            visible: (root.type === LV.ListItem.Action || root.type === LV.ListItem.ActionGroup || root.type === LV.ListItem.InlineEdit) && root.showPrimaryAction
        }
        ActionButton { actionName: "secondary"; visible: root.type === LV.ListItem.ActionGroup && root.showSecondaryAction }
        LV.DropdownButton { visible: root.type === LV.ListItem.ActionGroup && root.showMoreMenu; implicitWidth: LV.Theme.scaleMetric(72); text: qsTr("More"); tone: LV.AbstractButton.Default }
    }
}
