pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "editorPageDeck"
    required property var project
    required property var canvas
    required property Item activePage
    property bool navigationEnabled: true
    readonly property var boards: project.artboards

    function pageSize(pageIndex) {
        const direction = pageIndex < root.project.currentIndex ? -1 : 1
        let previousWidth = root.activePage.width
        let previousHeight = root.activePage.height
        let previousScale = 1
        let result = { width: 1, height: 1, scale: 1 }
        for (let i = root.project.currentIndex + direction; direction < 0 ? i >= pageIndex : i <= pageIndex; i += direction) {
            const board = root.boards[i] || ({})
            const fit = Math.min(root.width * 0.70 / Math.max(1, board.width || 1),
                root.height * 0.80 / Math.max(1, board.height || 1))
            const baseWidth = Math.max(1, board.width || 1) * fit
            const baseHeight = Math.max(1, board.height || 1) * fit
            const scale = Math.min(previousScale * 0.86,
                previousWidth * 0.86 / baseWidth, previousHeight * 0.86 / baseHeight)
            result = { width: baseWidth, height: baseHeight, scale: scale }
            previousWidth = baseWidth * scale
            previousHeight = baseHeight * scale
            previousScale = scale
        }
        return result
    }

    // Only the nearby pages need presentation images. The active page keeps
    // its native editor, including its original document coordinates.
    Repeater {
        model: root.canvas.pagePresentation ? Math.min(7, root.project.canvasCount) : 0
        delegate: Item {
            id: card
            required property int index
            readonly property int pageIndex: Math.max(0, Math.min(root.project.currentIndex - 3,
                root.project.canvasCount - 7)) + index
            readonly property var board: root.boards[pageIndex] || ({})
            readonly property int distance: Math.abs(pageIndex - root.project.currentIndex)
            property int previewCompletion: 0
            Connections {
                target: root.project
                function onPagePreviewReady(page) {
                    if (page === card.pageIndex) ++card.previewCompletion
                }
            }
            readonly property var pageSize: distance > 0 ? root.pageSize(pageIndex) : ({ width: 1, height: 1, scale: 1 })
            readonly property real depthScale: pageSize.scale
            readonly property real darkenAmount: Math.min(0.56, 0.20 + Math.max(0, distance - 1) * 0.14)
            readonly property real blurAmount: Math.min(0.60, 0.18 + Math.max(0, distance - 1) * 0.14)
            readonly property real baseWidth: pageSize.width
            readonly property real baseHeight: pageSize.height
            objectName: "editorPageCard" + pageIndex
            visible: distance > 0 && distance <= 3 && Boolean(board.id)
            width: baseWidth * depthScale
            height: baseHeight * depthScale
            x: pageIndex < root.project.currentIndex
                ? root.activePage.x - Math.max(0, root.activePage.x) * (0.62 + 0.18 * (distance - 1))
                : root.activePage.x + root.activePage.width
                    + Math.max(0, root.width - root.activePage.x - root.activePage.width) * (0.62 + 0.18 * (distance - 1)) - width
            y: root.activePage.y + root.activePage.height / 2 - height / 2
            z: 100 - distance
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 5
                anchors.bottomMargin: -5
                color: "#60000000"
            }
            Rectangle { anchors.fill: parent; color: LV.Theme.panelBackground06 }
            Image {
                id: pageImage
                objectName: "editorPageCardImage" + card.pageIndex
                anchors.fill: parent
                source: {
                    const revision = root.project.pagePreviewRevision
                    const completion = card.previewCompletion
                    return card.visible ? root.project.pagePreviewSource(card.pageIndex, 512, 512) : ""
                }
                fillMode: Image.PreserveAspectFit
                smooth: true
                cache: false
                layer.enabled: visible && GraphicsInfo.api !== GraphicsInfo.Software
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 24
                    blur: card.blurAmount
                    autoPaddingEnabled: false
                }
            }
            Rectangle {
                anchors.fill: parent
                color: "black"
                opacity: card.darkenAmount
            }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: LV.Theme.panelBackground10
            }
            LV.Label {
                objectName: "editorPageCardLabel" + card.pageIndex
                x: 4
                y: -22
                width: parent.width - 8
                height: 18
                text: qsTr("%1 · %2").arg(card.pageIndex + 1).arg(card.board.name || "")
                style: description
                elide: Text.ElideRight
                opacity: 1 - card.darkenAmount
            }
            MouseArea {
                anchors.fill: parent
                enabled: root.navigationEnabled && !root.canvas.liveStrokeActive
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.PageTab
                Accessible.name: qsTr("Page %1: %2").arg(card.pageIndex + 1).arg(card.board.name || "")
                onClicked: if (root.project.setCurrentIndex(card.pageIndex)) root.canvas.forceActiveFocus()
            }
        }
    }
}
