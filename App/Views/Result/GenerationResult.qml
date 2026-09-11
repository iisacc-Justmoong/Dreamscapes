pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import QtCore
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName: "generationResult"

    property var result: ({})
    property var results: []
    property string selectedImageSource: ""
    property bool detailVisible: false
    readonly property var galleryResults: results.length > 0 ? results
        : result.imageSource ? [result] : []
    readonly property var selectedResult: galleryResults.find(function(entry) {
        return entry.imageSource.toString() === selectedImageSource
    }) || result
    readonly property bool galleryVisible: galleryModel.count > 1 && !detailVisible
    property string statusText: ""
    property string errorText: ""
    property url previewSource: ""
    property string previewPrompt: ""
    property bool generationPending: false
    property bool generationCancellable: false
    readonly property bool showingPreview: !galleryVisible && !detailVisible && previewSource.toString().length > 0
    readonly property url imageSource: showingPreview ? previewSource : selectedResult.imageSource || ""
    readonly property var selectedTile: gallery.currentItem
    readonly property int imageStatus: galleryVisible
        ? selectedTile ? selectedTile.imageStatus : Image.Null : generatedImage.status
    readonly property bool imageReady: imageStatus === Image.Ready
    readonly property bool canSaveImage: visible && imageReady && !showingPreview
    property string saveFeedback: ""
    property PhotoLibraryExporter photoLibrary: PhotoLibraryExporter {
        objectName: "photoLibraryExporter"
    }
    signal backRequested()
    signal cancelRequested()
    signal newProjectRequested(url imageSource, var generationResult)

    // QuickGenerate derives its height from the shared 22 px controls.
    implicitWidth: 402
    implicitHeight: 478

    function openImageMenu(anchor, x, y) {
        if (canSaveImage && !saveDialog.visible)
            imageMenu.openFor(anchor, x, y)
    }

    function selectImage(index, openDetail) {
        if (index < 0 || index >= galleryResults.length)
            return
        selectedImageSource = galleryResults[index].imageSource.toString()
        detailVisible = openDetail
        if (openDetail) root.forceActiveFocus()
        else gallery.forceActiveFocus()
    }

    function goBack() {
        if (detailVisible && galleryModel.count > 1) {
            detailVisible = false
            gallery.forceActiveFocus()
        } else {
            backRequested()
        }
    }

    function syncGallery() {
        // Update the appended tail without resetting scroll position on each completion.
        const entries = galleryResults.map(function(entry) {
            return { imageSource: entry.imageSource.toString(), prompt: entry.prompt || "", isPreview: false }
        })
        if (generationPending)
            entries.push({ imageSource: "", prompt: "", isPreview: true })
        let shared = 0
        while (shared < galleryModel.count && shared < entries.length
            && galleryModel.get(shared).imageSource === entries[shared].imageSource
            && galleryModel.get(shared).isPreview === entries[shared].isPreview)
            ++shared
        if (shared < galleryModel.count)
            galleryModel.remove(shared, galleryModel.count - shared)
        for (let index = shared; index < entries.length; ++index)
            galleryModel.append(entries[index])
        if (selectedImageSource.length > 0 && !galleryResults.some(function(entry) {
            return entry.imageSource.toString() === selectedImageSource
        })) {
            selectedImageSource = ""
            detailVisible = false
        }
    }

    onGalleryResultsChanged: syncGallery()
    onGenerationPendingChanged: syncGallery()
    Component.onCompleted: syncGallery()
    Keys.onEscapePressed: goBack()

    ListModel { id: galleryModel }

    function saveImageToFile() {
        if (!canSaveImage || saveDialog.visible)
            return
        saveFeedback = ""
        // Keep the chosen image even if another generation finishes during the dialog.
        saveDialog.sourceImage = imageSource
        const name = fileExporter.suggestedFileName(imageSource)
        const suffix = name.lastIndexOf(".") >= 0 ? name.substring(name.lastIndexOf(".") + 1) : "png"
        saveDialog.defaultSuffix = suffix
        saveDialog.nameFilters = [qsTr("Image files (*.%1)").arg(suffix)]
        saveDialog.selectedFile = saveDialog.currentFolder.toString().replace(/\/$/, "") + "/" + encodeURIComponent(name)
        saveDialog.open()
    }

    function saveImageToPhotos() {
        if (!canSaveImage || !photoLibrary.supported || photoLibrary.busy)
            return
        saveFeedback = qsTr("Saving to Photos…")
        photoLibrary.save(imageSource)
    }

    onImageSourceChanged: {
        imageMenu.close()
        saveFeedback = ""
    }
    onCanSaveImageChanged: { if (!canSaveImage) imageMenu.close() }

    ImageFileExporter {
        id: fileExporter
        objectName: "imageFileExporter"
        onSaved: root.saveFeedback = qsTr("File saved")
        onFailed: function(message) { root.saveFeedback = message }
    }

    Connections {
        target: root.photoLibrary
        function onSaved(assetIdentifier) { root.saveFeedback = qsTr("Saved to Photos") }
        function onFailed(message) { root.saveFeedback = message }
    }

    Dialogs.FileDialog {
        id: saveDialog
        objectName: "saveImageDialog"
        property url sourceImage: ""
        title: qsTr("Save to File")
        fileMode: Dialogs.FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: fileExporter.save(sourceImage, selectedFile)
    }

    LV.ContextMenu {
        id: imageMenu
        objectName: "imageContextMenu"
        showIconSlot: false
        itemWidth: Math.max(0, Math.min(LV.Theme.scaleMetric(145),
            root.width - leftPadding - rightPadding - edgeMargin * 2))
        items: root.photoLibrary.supported
            ? [{ label: qsTr("Save to File") },
               { label: qsTr("Save to Photos"), enabled: !root.photoLibrary.busy }]
            : [{ label: qsTr("Save to File") }]
        onItemTriggered: function(index) {
            if (index === 0) root.saveImageToFile()
            else if (index === 1) root.saveImageToPhotos()
        }
    }

    LV.HStack {
        id: toolbar
        objectName: "resultToolbar"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: implicitHeight
        spacing: 0

        LV.IconButton {
            objectName: "resultBackButton"
            iconSource: Qt.resolvedUrl("Assets/right.svg")
            contentItem.rotation: 180
            tone: LV.AbstractButton.Borderless
            Accessible.name: root.detailVisible && galleryModel.count > 1 ? qsTr("Back to gallery") : qsTr("Back to generation")
            onClicked: root.goBack()
        }

        LV.Label {
            objectName: "galleryImageCount"
            visible: galleryModel.count > 1
            text: root.galleryResults.length === 1 ? qsTr("1 image") : qsTr("%1 images").arg(root.galleryResults.length)
            style: caption
            Layout.leftMargin: LV.Theme.gap8
        }

        Item { Layout.fillWidth: true }

        LV.LabelButton {
            objectName: "cancelGenerationButton"
            text: qsTr("Cancel")
            tone: LV.AbstractButton.Default
            visible: root.generationPending
            enabled: root.generationCancellable
            onClicked: root.cancelRequested()
        }

        LV.LabelButton {
            objectName: "newProjectButton"
            text: qsTr("New Project")
            tone: LV.AbstractButton.Primary
            enabled: root.imageReady && !root.generationPending && !root.showingPreview
            Accessible.name: qsTr("New project from generated image")
            onClicked: root.newProjectRequested(root.imageSource, root.selectedResult)
        }
    }

    Item {
        id: imageArea
        anchors.top: toolbar.bottom
        anchors.bottom: resultStatus.top
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true

        GridView {
            id: gallery
            objectName: "resultGallery"
            anchors.fill: parent
            anchors.topMargin: LV.Theme.gap2
            visible: root.galleryVisible
            clip: true
            readonly property int columns: Math.max(2, Math.floor(width / LV.Theme.scaleMetric(160)))
            cellWidth: width / columns
            cellHeight: cellWidth
            contentWidth: width
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            reuseItems: true
            cacheBuffer: cellHeight
            model: galleryModel
            currentIndex: root.selectedImageSource.length > 0
                ? root.galleryResults.findIndex(function(entry) { return entry.imageSource.toString() === root.selectedImageSource }) : -1
            keyNavigationEnabled: false
            Keys.onPressed: function(event) {
                let index = currentIndex < 0 ? 0 : currentIndex
                if (event.key === Qt.Key_Left) --index
                else if (event.key === Qt.Key_Right) ++index
                else if (event.key === Qt.Key_Up) index -= columns
                else if (event.key === Qt.Key_Down) index += columns
                else if (event.key === Qt.Key_Home) index = 0
                else if (event.key === Qt.Key_End) index = root.galleryResults.length - 1
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.selectImage(index, true)
                    event.accepted = true
                    return
                } else return
                index = Math.max(0, Math.min(root.galleryResults.length - 1, index))
                root.selectImage(index, false)
                positionViewAtIndex(index, GridView.Contain)
                event.accepted = true
            }
            Accessible.role: Accessible.Table
            Accessible.name: qsTr("Generated images")

            Controls.ScrollBar.vertical: Controls.ScrollBar {
                id: galleryScrollBar
                objectName: "galleryScrollBar"
                policy: Controls.ScrollBar.AsNeeded
                contentItem: Rectangle {
                    implicitWidth: LV.Theme.gap8
                    implicitHeight: LV.Theme.scaleMetric(40)
                    radius: width / 2
                    color: LV.Theme.textPrimary
                    opacity: galleryScrollBar.pressed ? 0.8 : 0.4
                }
            }

            delegate: Item {
                id: tile
                required property int index
                required property string imageSource
                required property string prompt
                required property bool isPreview
                readonly property int imageStatus: thumbnail.status
                readonly property bool imageReady: thumbnail.status === Image.Ready && !isPreview
                width: gallery.cellWidth
                height: gallery.cellHeight
                Accessible.role: Accessible.Button
                Accessible.name: isPreview ? qsTr("Generating image") : qsTr("Generated image %1").arg(index + 1)
                Accessible.description: isPreview ? root.previewPrompt : prompt
                Accessible.onPressAction: { if (!tile.isPreview) root.selectImage(tile.index, true) }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: LV.Theme.gap2 / 2
                    color: LV.Theme.surfaceSolid

                    Image {
                        id: thumbnail
                        objectName: "galleryThumbnail"
                        anchors.fill: parent
                        source: tile.isPreview ? root.previewSource : tile.imageSource
                        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                        asynchronous: true
                        cache: !tile.isPreview
                        retainWhileLoading: tile.isPreview
                        fillMode: Image.PreserveAspectCrop
                        clip: true
                    }

                    LV.Label {
                        anchors.centerIn: parent
                        width: parent.width - LV.Theme.gap8 * 2
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        style: caption
                        visible: thumbnail.status !== Image.Ready
                        text: thumbnail.status === Image.Error ? qsTr("Image unavailable")
                            : tile.isPreview ? qsTr("Generating…") : qsTr("Loading image…")
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: tile.GridView.isCurrentItem ? LV.Theme.gap2 : 0
                        border.color: LV.Theme.accent
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        enabled: !tile.isPreview
                        onTapped: function(eventPoint, button) {
                            root.selectImage(tile.index, button !== Qt.RightButton)
                            if (button === Qt.RightButton)
                                root.openImageMenu(tile, eventPoint.position.x, eventPoint.position.y)
                        }
                        onLongPressed: {
                            root.selectImage(tile.index, false)
                            root.openImageMenu(tile, point.position.x, point.position.y)
                        }
                    }
                }
            }
        }

        Image {
            id: generatedImage
            objectName: "generatedImage"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: Math.max(0, root.detailVisible ? imageArea.height : Math.min(LV.Theme.scaleMetric(242), imageArea.height))
            visible: !root.galleryVisible
            source: root.galleryVisible ? "" : root.imageSource
            asynchronous: true
            cache: !root.showingPreview
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            clip: true
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Generated image")
            Accessible.description: root.showingPreview ? root.previewPrompt : root.selectedResult.prompt || ""

            Item {
                id: imageInteraction
                objectName: "imageSaveInteraction"
                anchors.centerIn: parent
                width: Math.min(generatedImage.width, generatedImage.paintedWidth)
                height: Math.min(generatedImage.height, generatedImage.paintedHeight)
                enabled: root.canSaveImage

                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onTapped: function(eventPoint, button) {
                        if (button === Qt.RightButton)
                            root.openImageMenu(imageInteraction, eventPoint.position.x, eventPoint.position.y)
                    }
                    onLongPressed: root.openImageMenu(imageInteraction, point.position.x, point.position.y)
                }
            }
        }
    }

    LV.Label {
        id: resultStatus
        objectName: "resultStatus"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: LV.Theme.gap10
        anchors.rightMargin: LV.Theme.gap10
        height: visible ? implicitHeight + LV.Theme.gap8 : 0
        visible: text.length > 0
        style: caption
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 2
        elide: Text.ElideRight
        text: root.saveFeedback.length > 0 ? root.saveFeedback
            : root.imageStatus === Image.Error ? qsTr("The generated image could not be loaded.")
            : root.errorText.length > 0 ? root.errorText
            : root.imageStatus === Image.Loading ? qsTr("Loading image…")
            : root.statusText
    }
}
