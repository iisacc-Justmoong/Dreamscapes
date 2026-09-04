import QtQuick
import LVRS 1.0 as LV

Loader {
    objectName: "mainLoader"
    source: LV.Platform.mobile ? "Views/Mobile.qml" : "Views/Desktop.qml"

    function quitIfLoadFailed() {
        if (status === Loader.Error)
            Qt.exit(1)
    }

    onStatusChanged: quitIfLoadFailed()
    Component.onCompleted: quitIfLoadFailed()
}
