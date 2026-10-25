pragma ComponentBehavior: Bound
import QtQuick
import ".." as Frames
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Item {
    id: root
    readonly property Item panelFrame: Frames.PanelFrame { parent: root; panelId: "comparison" }
    objectName: "reviewCompare"
    property var entries: []
    property bool syncView: true
    property real zoom: 0
    property real panX: 0
    property real panY: 0
    signal closeRequested()
    onSyncViewChanged: {
        if (!syncView) {
            for (let index = 0; index < tiles.count; ++index) {
                const tile = tiles.itemAt(index)
                if (tile) { tile.localZoom = zoom; tile.localX = panX; tile.localY = panY }
            }
        } else if (tiles.count) {
            const tile = tiles.itemAt(0)
            if (tile) { zoom = tile.localZoom; panX = tile.localX; panY = tile.localY }
        }
    }
    function resetView(value) {
        zoom = value; panX = 0; panY = 0
        for (let index = 0; index < tiles.count; ++index) {
            const tile = tiles.itemAt(index)
            if (tile) { tile.localZoom = value; tile.localX = 0; tile.localY = 0 }
        }
    }
    Rectangle { anchors.fill: parent; color: LV.Theme.surfaceSolid }
    LV.VStack {
        anchors.fill: parent
        anchors.margins: LV.Theme.gap8
        spacing: LV.Theme.gap8
        Flickable {
            objectName: "reviewCompareToolsViewport"
            Layout.fillWidth: true
            Layout.preferredHeight: compareTools.implicitHeight
            contentWidth: compareTools.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            Controls.ScrollBar.horizontal: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            LV.HStack {
            id: compareTools
            width: Math.max(parent.width,implicitWidth)
            LV.Label { text: qsTr("Compare · %1 images").arg(root.entries.length); style: body; Layout.fillWidth: true }
            LV.LabelButton { objectName: "reviewCompareSync"; text: root.syncView ? qsTr("✓ Sync view") : qsTr("Sync view"); tone: LV.AbstractButton.Default; onClicked: root.syncView = !root.syncView }
            LV.LabelButton { objectName: "reviewCompareFit"; text: qsTr("Fit"); tone: LV.AbstractButton.Default; onClicked: root.resetView(0) }
            LV.LabelButton { objectName: "reviewCompareActual"; text: "100%"; tone: LV.AbstractButton.Default; onClicked: root.resetView(1) }
            LV.LabelButton { objectName: "reviewCompareClose"; text: qsTr("Close"); tone: LV.AbstractButton.Default; onClicked: root.closeRequested() }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: Math.max(1, root.width < 700 || root.entries.length > 2 ? 2 : root.entries.length)
            columnSpacing: LV.Theme.gap8
            rowSpacing: LV.Theme.gap8
            Repeater {
                id: tiles
                model: root.entries
                delegate: Item {
                    id: tile
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    Layout.minimumHeight: 0
                    property real localZoom: 0
                    property real localX: 0
                    property real localY: 0
                    LV.Label {
                        id: title
                        width: parent.width
                        text: qsTr("%1 · Seed %2").arg(tile.index + 1).arg(tile.modelData.seed === undefined ? qsTr("Unavailable") : tile.modelData.seed)
                        style: caption
                        elide: Text.ElideRight
                    }
                    ReviewMediaView {
                        objectName: "reviewCompareMedia" + tile.index
                        anchors.top: title.bottom
                        anchors.bottom: parent.bottom
                        width: parent.width
                        source: tile.modelData.imageSource || ""
                        pixelWidth: tile.modelData.width || 0
                        pixelHeight: tile.modelData.height || 0
                        interactive: true
                        zoom: root.syncView ? root.zoom : tile.localZoom
                        panX: root.syncView ? root.panX : tile.localX
                        panY: root.syncView ? root.panY : tile.localY
                        onZoomRequested: function(value) { if (root.syncView) root.zoom = value; else tile.localZoom = value }
                        onPanRequested: function(x, y) { if (root.syncView) { root.panX = x; root.panY = y } else { tile.localX = x; tile.localY = y } }
                    }
                }
            }
        }
        LV.Label { text: qsTr("Preview · 100% uses available preview pixels · Wheel to zoom · Drag to pan"); style: caption; Layout.fillWidth: true; elide: Text.ElideRight }
    }
}
