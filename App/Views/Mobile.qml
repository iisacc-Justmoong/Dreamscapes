import QtQuick
import LVRS 1.0 as LV

LV.ApplicationWindow {
    objectName: "mobileWindow"
    title: "Dreamscapes"
    width: 390
    height: 844
    desktopMinWidth: 320
    desktopMinHeight: 480
    mobileMinWidth: 320
    mobileMinHeight: 480
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
