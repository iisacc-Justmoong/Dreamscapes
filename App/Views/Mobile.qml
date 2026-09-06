import QtQuick
import LVRS 1.0 as LV
import "Home"

LV.ApplicationWindow {
    id: window
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
    windowDragHandleEnabled: false

    signal generateRequested(string prompt, string mediaType, string aspectRatio)

    QuickGenerate {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: window.mobileSystemSafeTopInset
        anchors.leftMargin: window.mobileSystemSafeLeftInset
        anchors.rightMargin: window.mobileSystemSafeRightInset
        height: implicitHeight
        onGenerateRequested: function(prompt, mediaType, aspectRatio) {
            window.generateRequested(prompt, mediaType, aspectRatio)
        }
    }
}
