import QtQuick
import LVRS 1.0 as LV

LV.ApplicationWindow {
    objectName: "desktopWindow"
    title: "Dreamscapes"
    width: 960
    height: 640
    desktopMinWidth: 640
    desktopMinHeight: 480
    transientParent: null
    visible: true
    navigationEnabled: false
    useInternalPageStack: false

    LV.Label {
        objectName: "helloLabel"
        anchors.centerIn: parent
        text: qsTr("Hello world!")
        style: title
    }
}
