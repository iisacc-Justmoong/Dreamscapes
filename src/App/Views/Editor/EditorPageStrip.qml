pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "editorPageStrip"
    required property var project
    required property var canvas
    property bool navigationEnabled: true
    readonly property var boards: project.artboards
    implicitHeight: canvas.pagePresentation ? 76 : 0
    visible: canvas.pagePresentation
    onVisibleChanged: if (!visible && root.project) root.project.cancelPagePreviews()

    Rectangle { width: parent.width; height: 1; color: LV.Theme.panelBackground10 }
    ListView {
        id: pages
        objectName: "editorPagePreviewList"
        anchors.fill: parent
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 176
        model: root.visible ? root.project.canvasCount : 0
        currentIndex: root.project.currentIndex
        function revealCurrent() {
            if (root.visible && count > 0)
                positionViewAtIndex(root.project.currentIndex, ListView.Contain)
        }
        function restoreCurrent() {
            if (!root.visible || count <= 0) return
            // A model reset changes the view's index. Reinstall the binding
            // while restoring the selected project page.
            currentIndex = Qt.binding(function() { return root.project.currentIndex })
            revealCurrent()
        }
        onCurrentIndexChanged: Qt.callLater(revealCurrent)
        onCountChanged: Qt.callLater(restoreCurrent)
        onVisibleChanged: Qt.callLater(restoreCurrent)
        onWidthChanged: Qt.callLater(revealCurrent)
        Component.onCompleted: Qt.callLater(restoreCurrent)

        delegate: LV.AbstractButton {
            id: preview
            required property int index
            readonly property var board: root.boards[index] || ({})
            readonly property bool selected: index === root.project.currentIndex
            property int previewCompletion: 0
            Connections {
                target: root.project
                function onPagePreviewReady(page) {
                    if (page === preview.index) ++preview.previewCompletion
                }
            }
            objectName: "editorPagePreview" + index
            width: 88
            height: 68
            horizontalPadding: 0
            verticalPadding: 0
            cornerRadius: 4
            enabled: root.navigationEnabled && !root.canvas.liveStrokeActive
            Accessible.name: qsTr("Page %1: %2").arg(index + 1).arg(board.name || "")
            onClicked: if (root.project.setCurrentIndex(index)) root.canvas.forceActiveFocus()
            background: Rectangle {
                radius: 4
                color: preview.selected ? LV.Theme.panelBackground08 : preview.down ? LV.Theme.panelBackground06 : "transparent"
                border.width: preview.selected ? 1 : 0
                border.color: LV.Theme.accent
            }
            contentItem: Item {
                Rectangle {
                    x: 8; y: 4; width: 72; height: 48
                    color: LV.Theme.panelBackground06
                }
                Image {
                    objectName: "editorPagePreviewImage" + preview.index
                    x: 8; y: 4; width: 72; height: 48
                    source: {
                        const revision = root.project.pagePreviewRevision
                        const completion = preview.previewCompletion
                        return root.visible && preview.visible ? root.project.pagePreviewSource(preview.index, 144, 96) : ""
                    }
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    cache: false
                }
                LV.Label {
                    x: 4; y: 54; width: parent.width - 8; height: 14
                    text: qsTr("%1 · %2").arg(preview.index + 1).arg(preview.board.name || "")
                    style: description
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}
