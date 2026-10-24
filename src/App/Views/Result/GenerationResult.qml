pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import QtCore
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import "../Home"

Item {
    id: root
    objectName: "generationResult"
    property var result: ({})
    property var results: []
    property var jobs: []
    property var inferenceStatus: ({})
    property string statusText: ""
    property string errorText: ""
    property url previewSource: ""
    property string previewPrompt: ""
    property bool generationPending: false
    property bool cancelAvailable: generationPending
    property bool detailVisible: false
    property bool compareVisible: false
    property bool queueOpen: false
    property bool inspectorOpen: false
    property var compareEntries: []
    property string selectedImageSource: ""
    property string saveFeedback: ""
    readonly property var galleryResults: results.length ? results : result.imageSource ? [result] : []
    readonly property var selectedResult: review.focusedEntry
    readonly property bool galleryVisible: !detailVisible && !compareVisible
    readonly property bool showingPreview: review.totalCount === 0 && previewSource.toString().length > 0
    readonly property url imageSource: selectedResult.imageSource || (showingPreview ? previewSource : "")
    readonly property bool selectedIsVideo: selectedResult.mediaType === "Video"
    readonly property url mediaSource: selectedResult.documentSource || selectedResult.mediaSource || selectedResult.imageSource || ""
    readonly property var selectedTile: gallery.currentItem
    readonly property int imageStatus: detailVisible ? detailMedia.imageStatus : selectedTile ? selectedTile.imageStatus : showingPreview ? livePreview.status : Image.Null
    readonly property bool imageReady: imageStatus === Image.Ready
    readonly property bool canSaveImage: visible && !showingPreview && review.focusedKey.length > 0 && mediaSource.toString().length > 0
    readonly property bool wide: width >= 1180
    readonly property bool showQueue: width >= 860
    readonly property alias reviewModel: review
    property PhotoLibraryExporter photoLibrary: PhotoLibraryExporter { objectName: "photoLibraryExporter" }
    signal backRequested()
    signal cancelRequested()
    signal cancelJobRequested(string id)
    signal failureRequested(string id)
    signal operationFailed(string operation, string message, var parameters)
    signal imagesRequested(var images)
    implicitWidth: 1440
    implicitHeight: 760
    clip: true
    onVisibleChanged: if (visible) Qt.callLater(focusResults)

    GenerationReviewModel {
        id: review
        objectName: "generationReviewModel"
        entries: root.galleryResults
        onProjectionAboutToChange: if (gallery) gallery.rememberPosition()
        onProjectionChanged: if (gallery) Qt.callLater(gallery.restorePosition)
        onStateChanged: root.selectedImageSource = focusedEntry.imageSource ? focusedEntry.imageSource.toString() : ""
    }
    function selectImage(index, openDetail) {
        if (index < 0 || index >= review.count) return
        review.focusAt(index)
        if (openDetail) {
            gallery.rememberPosition(); detailVisible = true
            detailMedia.zoom = 0; detailMedia.panX = 0; detailMedia.panY = 0; root.forceActiveFocus()
        } else gallery.forceActiveFocus()
    }
    function activateImage(index, modifiers) {
        if (modifiers || (review.selectionMode && imageSelection.toggleOnClick)) review.selectAt(index, modifiers || Qt.ControlModifier)
        else review.focusAt(index)
        gallery.forceActiveFocus()
    }
    function rememberGalleryPosition() { gallery.rememberPosition() }
    function reportPlaybackFailure(message) {
        saveFeedback = message
        operationFailed("Preview video",message,{source:mediaSource})
    }
    function focusResults() {
        if (!visible) return
        if (detailVisible || compareVisible || !gallery.visible) root.forceActiveFocus()
        else gallery.forceActiveFocus()
    }
    function showEntry(entry) {
        if (review.keyForEntry(entry) !== review.focusedKey)
            review.focusSource((entry.documentSource || entry.mediaSource || entry.imageSource || "").toString())
        compareVisible = false
        gallery.rememberPosition(); detailVisible = true
        detailMedia.zoom = 0; detailMedia.panX = 0; detailMedia.panY = 0
        root.forceActiveFocus()
    }
    function goBack() {
        if (compareVisible || detailVisible || queueOpen || inspectorOpen) {
            compareVisible = false; detailVisible = false; queueOpen = false; inspectorOpen = false
            gallery.forceActiveFocus(); Qt.callLater(gallery.restorePosition)
        } else backRequested()
    }
    function resetPresentation() {
        // Presentation changes preserve this session's review decisions.
        gallery.rememberPosition()
        detailVisible = false; compareVisible = false; queueOpen = false; inspectorOpen = false
        imageMenu.close(); saveDialog.close(); exportDialog.close(); saveFeedback = ""
    }
    function editEntries(entries) {
        if (!entries.length || entries.some(entry => entry.mediaType === "Video" || entry.mediaType === "Canvas")) {
            saveFeedback = qsTr("Select only images for the image editor; videos can be previewed or exported."); return
        }
        imagesRequested(entries)
    }
    function compareImages(entries) {
        if (entries.length < 2 || entries.length > 4 || entries.some(entry => entry.mediaType === "Video" || entry.mediaType === "Canvas")) {
            saveFeedback = qsTr("Select 2–4 images to compare."); return
        }
        gallery.rememberPosition(); compareEntries = entries.slice(); comparison.resetView(0)
        compareVisible = true; root.forceActiveFocus()
    }
    function handleKey(event) {
        if (event.key === Qt.Key_Escape && (detailVisible || compareVisible || queueOpen || inspectorOpen)) {
            goBack(); event.accepted = true; return
        }
        if (!gallery.activeFocus && !root.activeFocus && !detailVisible && !compareVisible) return
        const command = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)
        if (event.key === Qt.Key_Escape) {
            if (detailVisible || compareVisible || queueOpen || inspectorOpen) goBack()
            else if (review.selectedCount || review.selectionMode) review.clearSelection()
            else goBack()
        } else if (event.key === Qt.Key_A && command) review.selectAll()
        else if (command || (event.modifiers & Qt.AltModifier)) return
        else if (event.key === Qt.Key_F) review.toggleReview("shortlisted")
        else if (event.key === Qt.Key_X) review.toggleReview("rejected")
        else if (event.key === Qt.Key_C) compareImages(review.selectedEntries)
        else if (event.key === Qt.Key_Space) {
            if (detailVisible) goBack()
            else if (review.focusedKey.length) showEntry(review.focusedEntry)
            else selectImage(0,true)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) editEntries(review.actionEntries)
        else {
            if (detailVisible || compareVisible || !review.count) return
            let index = Math.max(0,review.focusedIndex)
            if (event.key === Qt.Key_Left) --index
            else if (event.key === Qt.Key_Right) ++index
            else if (event.key === Qt.Key_Up) index -= gallery.columns
            else if (event.key === Qt.Key_Down) index += gallery.columns
            else if (event.key === Qt.Key_Home) index = 0
            else if (event.key === Qt.Key_End) index = review.count - 1
            else return
            index = Math.max(0,Math.min(review.count - 1,index))
            if (event.modifiers & Qt.ShiftModifier) review.selectAt(index,event.modifiers)
            else review.focusAt(index)
            gallery.restoreSavedPosition = false
            gallery.positionViewAtIndex(index,GridView.Contain)
        }
        event.accepted = true
    }
    Keys.onPressed: function(event) { root.handleKey(event) }
    function openImageMenu(anchor,x,y) {
        if (!review.focusedKey.length) return
        imageMenu.targets = review.isSelected(review.focusedEntry) ? review.selectedEntries.slice() : [review.focusedEntry]
        imageMenu.openFor(anchor,x,y)
    }
    function saveImageToFile() {
        if (!canSaveImage || saveDialog.visible || fileExporter.busy) return
        saveFeedback = ""
        saveDialog.sourceImage = mediaSource; saveDialog.sourceIsVideo = selectedIsVideo
        const name = fileExporter.suggestedFileName(mediaSource)
        const suffix = name.lastIndexOf(".") >= 0 ? name.substring(name.lastIndexOf(".") + 1) : "png"
        saveDialog.defaultSuffix = suffix
        saveDialog.nameFilters = [selectedIsVideo ? qsTr("Video files (*.mp4)") : qsTr("Image files (*.%1)").arg(suffix)]
        saveDialog.selectedFile = saveDialog.currentFolder.toString().replace(/\/$/,"") + "/" + encodeURIComponent(name)
        saveDialog.open()
    }
    function saveImageToPhotos() {
        if (selectedIsVideo || !canSaveImage || !photoLibrary.supported || photoLibrary.busy) return
        saveFeedback = qsTr("Saving to Photos…"); photoLibrary.save(mediaSource)
    }
    function exportSelection() {
        if (batchExporter.busy || !review.actionEntries.length) return
        exportDialog.entries = review.actionEntries.slice(); exportDialog.open()
    }
    function filterLabel(key) {
        if (key === "all") return qsTr("All %1").arg(review.totalCount)
        if (key === "shortlisted") return qsTr("Shortlisted %1").arg(review.shortlistedCount)
        if (key === "rejected") return qsTr("Rejected %1").arg(review.rejectedCount)
        return {unreviewed:qsTr("Unreviewed"),images:qsTr("Images"),videos:qsTr("Videos")}[key] || key
    }
    ImageFileExporter {
        id: fileExporter
        objectName: "imageFileExporter"
        onSaved: root.saveFeedback = qsTr("File saved")
        onFailed: function(message) { root.saveFeedback = message; root.operationFailed("Save media",message,{source:saveDialog.sourceImage,destination:saveDialog.selectedFile}) }
        onRejected: function(message) { root.saveFeedback = message }
    }
    GenerationReviewExport {
        id: batchExporter
        objectName: "reviewBatchExporter"
        onCompleted: function(summary) {
            root.saveFeedback = qsTr("Export: %1 succeeded · %2 failed").arg(summary.succeeded).arg(summary.failed)
            if (summary.failed) root.operationFailed("Export review selection",summary.errors.join("\n"),summary)
        }
        onRejected: function(message) { root.saveFeedback = message }
    }
    Connections {
        target: root.photoLibrary
        function onSaved(assetIdentifier) { root.saveFeedback = qsTr("Saved to Photos") }
        function onFailed(message) { root.saveFeedback = message; root.operationFailed("Save to Photos",message,{source:root.mediaSource}) }
        function onRejected(message) { root.saveFeedback = message }
    }
    Dialogs.FileDialog {
        id: saveDialog
        objectName: "saveImageDialog"
        property url sourceImage: ""
        property bool sourceIsVideo: false
        title: qsTr("Save to File")
        fileMode: Dialogs.FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: {
            root.saveFeedback = qsTr("Saving file…")
            if (sourceIsVideo) fileExporter.saveVideo(sourceImage,selectedFile)
            else fileExporter.save(sourceImage,selectedFile)
        }
    }
    Dialogs.FolderDialog {
        id: exportDialog
        objectName: "reviewExportDialog"
        property var entries: []
        title: qsTr("Export selected originals")
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: batchExporter.start(entries,selectedFolder)
    }
    LV.ContextMenu {
        id: imageMenu
        objectName: "imageContextMenu"
        property var targets: []
        showIconSlot: false
        itemWidth: Math.min(280,Math.max(150,root.width - LV.Theme.gap24))
        items: [
            {label:qsTr("Shortlist · %1 results").arg(targets.length)},
            {label:qsTr("Reject · %1 results").arg(targets.length)},
            {label:qsTr("Reset review · %1 results").arg(targets.length)},
            {label:qsTr("Compare · %1 images").arg(targets.length),enabled:targets.length >= 2 && targets.length <= 4 && targets.every(entry => entry.mediaType !== "Video")},
            {label:qsTr("Edit · %1 images").arg(targets.length),enabled:targets.length > 0 && targets.every(entry => entry.mediaType !== "Video")},
            {label:qsTr("Save current to File"),enabled:root.canSaveImage && !fileExporter.busy},
            {label:qsTr("Save current to Photos"),enabled:root.canSaveImage && root.photoLibrary.supported && !root.selectedIsVideo && !root.photoLibrary.busy}]
        onItemTriggered: function(index) {
            if (index < 3) targets.forEach(entry => review.setEntryReview(review.keyForEntry(entry),["shortlisted","rejected","unreviewed"][index]))
            else if (index === 3) root.compareImages(targets)
            else if (index === 4) root.editEntries(targets)
            else if (index === 5) root.saveImageToFile()
            else if (index === 6) root.saveImageToPhotos()
        }
    }
    LV.ContextMenu {
        id: sortMenu
        showIconSlot: false
        items: [{label:qsTr("Arrival order")},{label:qsTr("Newest first")},{label:qsTr("Seed")}]
        onItemTriggered: function(index) { review.sort = ["arrival","newest","seed"][index]; gallery.forceActiveFocus() }
    }
    LV.HStack {
        id: toolbar
        objectName: "resultToolbar"
        anchors.top: parent.top
        width: parent.width
        height: implicitHeight + LV.Theme.gap8
        spacing: LV.Theme.gap8
        LV.LabelButton { objectName: "reviewBackButton"; text: qsTr("Back"); tone: LV.AbstractButton.Default; onClicked: root.backRequested() }
        LV.Label { text: qsTr("Generation review"); style: body; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
        LV.Label { objectName: "galleryImageCount"; text: qsTr("%1 results").arg(review.totalCount); style: caption; visible: root.width >= 500 }
        LV.LabelButton { objectName: "reviewQueueButton"; text: qsTr("Queue"); visible: !root.showQueue; tone: LV.AbstractButton.Default; onClicked: { root.queueOpen = !root.queueOpen; root.inspectorOpen = false } }
        LV.LabelButton { objectName: "reviewInspectorButton"; text: qsTr("Inspect"); visible: !root.wide; tone: LV.AbstractButton.Default; onClicked: { root.inspectorOpen = !root.inspectorOpen; root.queueOpen = false } }
        LV.LabelButton { objectName: "cancelGenerationButton"; text: qsTr("Cancel batch"); visible: root.cancelAvailable; tone: LV.AbstractButton.Default; onClicked: root.cancelRequested() }
    }
    Item {
        id: workspace
        visible: !root.detailVisible && !root.compareVisible
        anchors.top: toolbar.bottom
        anchors.bottom: actionViewport.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottomMargin: LV.Theme.gap8
        ReviewQueue {
            id: queuePanel
            objectName: "reviewQueuePanel"
            anchors.left: parent.left
            height: parent.height
            width: root.wide ? 260 : 208
            visible: root.showQueue
            jobs: root.jobs; inference: root.inferenceStatus; previewSource: root.previewSource; selectedJob: review.jobFilter
            onJobRequested: function(id) { review.jobFilter = id; gallery.forceActiveFocus() }
            onCancelRequested: function(id) { root.cancelJobRequested(id) }
            onFailureRequested: function(id) { root.failureRequested(id) }
        }
        ReviewInspector {
            id: inspector
            objectName: "reviewInspectorPanel"
            anchors.right: parent.right
            height: parent.height
            width: 320
            visible: root.wide
            review: root.reviewModel
            onPreviewRequested: root.showEntry(review.focusedEntry)
            onSaveRequested: root.saveImageToFile()
            onPlaybackFailed: function(message) { root.reportPlaybackFailure(message) }
        }
        Item {
            id: resultsArea
            objectName: "reviewResultsArea"
            anchors.left: root.showQueue ? queuePanel.right : parent.left
            anchors.right: root.wide ? inspector.left : parent.right
            anchors.leftMargin: root.showQueue ? LV.Theme.gap12 : 0
            anchors.rightMargin: root.wide ? LV.Theme.gap12 : 0
            height: parent.height
            clip: true
            Flickable {
                id: filterViewport
                objectName: "reviewFilterViewport"
                width: parent.width
                height: filters.implicitHeight + LV.Theme.gap8
                contentWidth: filters.implicitWidth
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                LV.HStack {
                    id: filters
                    spacing: LV.Theme.gap4
                    Repeater {
                        model: ["all","shortlisted","unreviewed","rejected","images","videos"]
                        delegate: LV.LabelButton {
                            required property string modelData
                            objectName: "reviewFilter_" + modelData
                            text: (review.filter === modelData ? "✓ " : "") + root.filterLabel(modelData)
                            tone: LV.AbstractButton.Default
                            onClicked: { review.filter = modelData; gallery.forceActiveFocus() }
                        }
                    }
                    LV.DropdownButton {
                        id: sortButton
                        objectName: "reviewSortButton"
                        text: review.sort === "seed" ? qsTr("Seed") : review.sort === "newest" ? qsTr("Newest") : qsTr("Arrival")
                        tone: LV.AbstractButton.Default
                        onClicked: sortMenu.openFor(sortButton,0,sortButton.height)
                    }
                    LV.LabelButton { visible: review.jobFilter.length > 0; text: qsTr("Clear job filter"); tone: LV.AbstractButton.Default; onClicked: review.jobFilter = "" }
                }
            }
            GridView {
                id: gallery
                objectName: "resultGallery"
                anchors.top: filterViewport.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                clip: true
                reuseItems: true
                readonly property int columns: Math.max(2,Math.floor(width / 160))
                cellWidth: width / columns
                cellHeight: cellWidth
                cacheBuffer: cellHeight
                contentWidth: width
                model: review
                currentIndex: review.focusedIndex
                keyNavigationEnabled: false
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                visible: review.count > 0
                Accessible.role: Accessible.Table
                Accessible.name: qsTr("Generation results")
                property real savedContentY: 0
                property string savedTopKey: ""
                property real savedOffset: 0
                property bool restoreSavedPosition: false
                property bool hasPresented: false
                property string presentedProjection: "all|arrival|"
                property var positions: ({})
                Component.onCompleted: hasPresented = visible
                function rememberPosition() {
                    if (!hasPresented || restoreSavedPosition) return
                    savedContentY = contentY
                    const index = indexAt(1,contentY + 1)
                    savedTopKey = review.keyAt(index)
                    savedOffset = index >= 0 ? contentY - originY - Math.floor(index / columns) * cellHeight : 0
                    positions[presentedProjection] = {key:savedTopKey,offset:savedOffset,y:savedContentY}
                    restoreSavedPosition = true
                }
                function restorePosition() {
                    if (!visible || !restoreSavedPosition) return
                    forceLayout()
                    const next = review.filter + "|" + review.sort + "|" + review.jobFilter
                    const retained = positions[next]
                    if (next !== presentedProjection && retained) {
                        savedTopKey = retained.key; savedOffset = retained.offset; savedContentY = retained.y
                    }
                    presentedProjection = next
                    const index = review.indexForKey(savedTopKey)
                    const target = index >= 0 ? originY + Math.floor(index / columns) * cellHeight + savedOffset : savedContentY
                    contentY = Math.max(originY,Math.min(target,originY + Math.max(0,contentHeight - height)))
                    savedContentY = contentY; restoreSavedPosition = false
                }
                onVisibleChanged: { if (!visible) rememberPosition(); else { hasPresented = true; Qt.callLater(restorePosition) } }
                onContentYChanged: if (visible && !restoreSavedPosition) savedContentY = contentY
                onMovementStarted: restoreSavedPosition = false
                onHeightChanged: if (visible) Qt.callLater(restorePosition)
                Keys.onPressed: function(event) { root.handleKey(event) }
                WheelHandler {
                    target: null
                    onWheel: function(event) {
                        const delta = event.pixelDelta.y || event.angleDelta.y / 120 * 60
                        if (!delta || gallery.contentHeight <= gallery.height) { event.accepted = false; return }
                        gallery.cancelFlick(); gallery.restoreSavedPosition = false
                        gallery.contentY = Math.max(gallery.originY,Math.min(gallery.originY + gallery.contentHeight - gallery.height,gallery.contentY - delta))
                        event.accepted = true
                    }
                }
                Controls.ScrollBar.vertical: Controls.ScrollBar { objectName: "galleryScrollBar"; policy: Controls.ScrollBar.AsNeeded }
                delegate: Item {
                    id: tile
                    required property int index
                    required property string reviewKey
                    required property var entry
                    required property string reviewState
                    required property bool selected
                    required property bool focused
                    required property int ordinal
                    required property url thumbnailSource
                    required property string mediaType
                    objectName: "resultImageTile" + index
                    width: gallery.cellWidth
                    height: gallery.cellHeight
                    readonly property int imageStatus: thumbnail.status
                    readonly property bool imageReady: imageStatus === Image.Ready
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("%1 %2 · %3").arg(mediaType).arg(ordinal).arg(reviewState)
                    Accessible.description: entry.prompt || ""
                    Accessible.onPressAction: root.activateImage(tile.index,0)
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: LV.Theme.gap2
                        color: LV.Theme.surfaceSolid
                        Image {
                            id: thumbnail
                            objectName: "galleryThumbnail"
                            anchors.fill: parent
                            source: tile.thumbnailSource
                            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectCrop
                            clip: true
                            opacity: tile.reviewState === "rejected" ? 0.45 : 1
                        }
                        LV.Label {
                            anchors.centerIn: parent
                            width: parent.width - LV.Theme.gap8
                            visible: thumbnail.status !== Image.Ready
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            style: caption
                            text: thumbnail.status === Image.Error ? qsTr("Preview unavailable") : qsTr("Loading…")
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: tileLabel.implicitHeight + LV.Theme.gap4
                            color: LV.Theme.surfaceSolid
                            opacity: 0.92
                            LV.Label {
                                id: tileLabel
                                anchors.fill: parent
                                anchors.margins: LV.Theme.gap2
                                text: qsTr("%1 · %2 · %3").arg(tile.ordinal.toString().padStart(2,"0")).arg(tile.mediaType).arg((tile.entry.id || qsTr("Unavailable")).slice(0,8))
                                style: caption
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: LV.Theme.gap4
                            width: stateLabel.implicitWidth + LV.Theme.gap8
                            height: stateLabel.implicitHeight + LV.Theme.gap4
                            radius: LV.Theme.gap4
                            color: LV.Theme.surfaceSolid
                            visible: tile.selected || tile.reviewState !== "unreviewed"
                            LV.Label {
                                id: stateLabel
                                anchors.centerIn: parent
                                text: (tile.selected ? "✓ " : "") + (tile.reviewState === "shortlisted" ? "★" : tile.reviewState === "rejected" ? "×" : "")
                                style: caption
                                color: tile.reviewState === "shortlisted" ? LV.Theme.accent : LV.Theme.textPrimary
                            }
                        }
                        Rectangle { anchors.fill: parent; color: "transparent"; border.width: tile.selected ? 2 : tile.focused ? 1 : 0; border.color: LV.Theme.accent }
                        TapHandler {
                            id: tileTap
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onTapped: function(point,button) {
                                if (button === Qt.RightButton) { review.focusAt(tile.index); root.openImageMenu(tile,point.position.x,point.position.y) }
                                else root.activateImage(tile.index,tileTap.point.modifiers)
                            }
                            onDoubleTapped: root.selectImage(tile.index,true)
                            onLongPressed: { review.focusAt(tile.index); root.openImageMenu(tile,point.position.x,point.position.y) }
                        }
                    }
                }
            }
            Image {
                id: livePreview
                objectName: "liveGenerationPreview"
                anchors.top: filterViewport.bottom
                anchors.bottom: emptyLabel.top
                width: parent.width
                source: visible ? root.previewSource : ""
                sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),Math.ceil(height * Screen.devicePixelRatio))
                asynchronous: true
                cache: false
                retainWhileLoading: true
                fillMode: Image.PreserveAspectFit
                visible: review.count === 0 && root.showingPreview
            }
            LV.Label {
                id: emptyLabel
                anchors.centerIn: parent
                width: Math.max(0,parent.width - LV.Theme.gap16)
                visible: review.count === 0
                style: caption
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: review.totalCount ? qsTr("No results match this filter. Selected results remain available below.")
                    : root.generationPending ? qsTr("Generation in progress · Completed results appear here as they arrive.") : qsTr("No results in this session. Start an image or video generation.")
            }
        }
    }
    Flickable {
        id: actionViewport
        objectName: "reviewActionViewport"
        anchors.bottom: resultStatus.top
        width: parent.width
        height: actions.implicitHeight + LV.Theme.gap4
        contentWidth: actions.implicitWidth
        contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        LV.HStack {
            id: actions
            spacing: LV.Theme.gap8
            LV.Label { objectName: "reviewSelectedCount"; text: qsTr("%1 selected%2").arg(review.selectedCount).arg(review.hiddenSelectedCount ? qsTr(" · %1 hidden").arg(review.hiddenSelectedCount) : ""); style: caption }
            ImageSelectionBar {
                id: imageSelection
                objectName: "resultImageSelection"
                entries: review.entries
                selectionBackend: review
                enabled: review.totalCount > 0
                onOpenRequested: function(images) { root.editEntries(images) }
            }
            LV.LabelButton { objectName: "reviewEditFocused"; visible: !review.selectedCount; text: qsTr("Edit focused"); enabled: !!review.focusedKey && !root.selectedIsVideo; tone: LV.AbstractButton.Default; onClicked: root.editEntries(review.actionEntries) }
            LV.LabelButton { objectName: "reviewShortlist"; text: qsTr("★ Shortlist (%1)").arg(review.actionEntries.length); enabled: review.actionEntries.length > 0; tone: LV.AbstractButton.Default; onClicked: { review.toggleReview("shortlisted"); gallery.forceActiveFocus() } }
            LV.LabelButton { objectName: "reviewReject"; text: qsTr("× Reject (%1)").arg(review.actionEntries.length); enabled: review.actionEntries.length > 0; tone: LV.AbstractButton.Default; onClicked: { review.toggleReview("rejected"); gallery.forceActiveFocus() } }
            LV.LabelButton { objectName: "reviewCompareButton"; text: qsTr("Compare (%1)").arg(review.selectedCount); enabled: review.selectedCount >= 2 && review.selectedCount <= 4 && review.selectedImages.length === review.selectedCount; tone: LV.AbstractButton.Default; onClicked: root.compareImages(review.selectedEntries) }
            LV.LabelButton { objectName: "reviewExportButton"; text: qsTr("Export (%1)").arg(review.actionEntries.length); enabled: review.actionEntries.length > 0 && !batchExporter.busy; tone: LV.AbstractButton.Default; onClicked: root.exportSelection() }
            LV.LabelButton { objectName: "saveVideoButton"; text: qsTr("Save video"); visible: root.selectedIsVideo; enabled: root.canSaveImage && !fileExporter.busy; tone: LV.AbstractButton.Default; onClicked: root.saveImageToFile() }
            LV.LabelButton { objectName: "reviewClearSelection"; text: qsTr("Clear selection"); enabled: review.selectedCount > 0; tone: LV.AbstractButton.Default; onClicked: review.clearSelection() }
        }
    }
    LV.Label {
        id: resultStatus
        objectName: "resultStatus"
        anchors.bottom: parent.bottom
        width: parent.width
        height: implicitHeight + LV.Theme.gap4
        style: caption
        elide: Text.ElideRight
        text: batchExporter.busy ? qsTr("Exporting %1 / %2…").arg(batchExporter.processed).arg(batchExporter.total)
            : root.saveFeedback || root.errorText || root.statusText || qsTr("F Shortlist · X Reject · Space Preview · C Compare · Enter Edit · Review decisions last for this session")
        color: root.errorText.length && !root.saveFeedback.length ? LV.Theme.accentRed : LV.Theme.textSecondary
    }
    Rectangle {
        anchors.fill: workspace
        color: LV.Theme.surfaceSolid
        visible: root.queueOpen || root.inspectorOpen
        z: 2
        LV.LabelButton { id: drawerClose; anchors.top: parent.top; anchors.right: parent.right; text: qsTr("Close"); tone: LV.AbstractButton.Default; onClicked: root.goBack() }
        ReviewQueue {
            anchors.top: drawerClose.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            visible: root.queueOpen
            jobs: root.jobs; inference: root.inferenceStatus; previewSource: root.previewSource; selectedJob: review.jobFilter
            onJobRequested: function(id) { review.jobFilter = id; root.goBack() }
            onCancelRequested: function(id) { root.cancelJobRequested(id) }
            onFailureRequested: function(id) { root.failureRequested(id) }
        }
        ReviewInspector {
            anchors.top: drawerClose.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            visible: root.inspectorOpen
            review: root.reviewModel
            onPreviewRequested: { root.inspectorOpen = false; root.showEntry(review.focusedEntry) }
            onSaveRequested: root.saveImageToFile()
            onPlaybackFailed: function(message) { root.reportPlaybackFailure(message) }
        }
    }
    Rectangle {
        anchors.fill: parent
        color: LV.Theme.surfaceSolid
        visible: root.detailVisible
        z: 3
        LV.HStack {
            id: detailTools
            width: parent.width
            LV.Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.selectedIsVideo ? qsTr("Video preview") : qsTr("Image preview"); style: body; elide: Text.ElideRight }
            LV.LabelButton { text: qsTr("Fit"); tone: LV.AbstractButton.Default; onClicked: { detailMedia.zoom = 0; detailMedia.panX = 0; detailMedia.panY = 0 } }
            LV.LabelButton { text: "100%"; tone: LV.AbstractButton.Default; onClicked: { detailMedia.zoom = 1; detailMedia.panX = 0; detailMedia.panY = 0 } }
            LV.LabelButton { text: qsTr("Close"); objectName: "reviewPreviewClose"; tone: LV.AbstractButton.Default; onClicked: root.goBack() }
        }
        ReviewMediaView {
            id: detailMedia
            objectName: "generatedImage"
            anchors.top: detailTools.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            source: root.imageSource
            mediaSource: root.mediaSource
            video: root.selectedIsVideo
            pixelWidth: root.selectedResult.width || 0
            pixelHeight: root.selectedResult.height || 0
            interactive: true
            onZoomRequested: function(value) { zoom = value }
            onPanRequested: function(x,y) { panX = x; panY = y }
            onPlaybackFailed: function(message) { root.reportPlaybackFailure(message) }
            TapHandler {
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onTapped: function(point,button) { if (button === Qt.RightButton) root.openImageMenu(detailMedia,point.position.x,point.position.y) }
                onLongPressed: root.openImageMenu(detailMedia,point.position.x,point.position.y)
            }
        }
    }
    ReviewCompare {
        id: comparison
        anchors.fill: parent
        entries: root.compareEntries
        visible: root.compareVisible
        z: 4
        onCloseRequested: root.goBack()
    }
}
