pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import "../Home"

Column {
    id: root
    property string label
    property string value
    property string placeholder
    property bool readOnly: false
    property alias inputItem: input.inputItem
    signal edited(string value)
    spacing: LV.Theme.gap6
    LV.Label { width: parent.width; visible: text.length > 0; text: root.label; style: caption }
    PromptField {
        id: input
        objectName: root.objectName + "Input"
        width: parent.width
        text: root.value
        placeholderText: root.placeholder
        readOnly: root.readOnly
        clearButtonVisible: false
        Accessible.name: root.label
        onTextEdited: next => root.edited(next)
    }
}
