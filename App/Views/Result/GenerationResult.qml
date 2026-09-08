pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import QtCore
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName: "generationResult"

    property var result: ({})
    property string statusText: ""
    property string errorText: ""
    property url previewSource: ""
    property string previewPrompt: ""
    property bool generationPending: false
    readonly property bool showingPreview: previewSource.toString().length > 0
    readonly property url imageSource: showingPreview ? previewSource : result.imageSource || ""
    readonly property bool imageReady: generatedImage.status === Image.Ready
    readonly property bool canSaveImage: visible && imageReady && !showingPreview
    property string saveFeedback: ""
    property PhotoLibraryExporter photoLibrary: PhotoLibraryExporter {
        objectName: "photoLibraryExporter"
    }
    signal backRequested()
    signal newProjectRequested(url imageSource, var generationResult)

    // Figma 31:81 reserves another 69 px below this item for QuickGenerate.
    implicitWidth: 402
    implicitHeight: 478

    function openImageMenu(x, y) {
        if (canSaveImage && !saveDialog.visible)
            imageMenu.openFor(imageInteraction, x, y)
    }

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
            Accessible.name: qsTr("Back to generation")
            onClicked: root.backRequested()
        }

        Item { Layout.fillWidth: true }

        LV.LabelButton {
            objectName: "newProjectButton"
            text: qsTr("New Project")
            tone: LV.AbstractButton.Primary
            enabled: root.imageReady && !root.generationPending && !root.showingPreview
            Accessible.name: qsTr("New project from generated image")
            onClicked: root.newProjectRequested(root.imageSource, root.result)
        }
    }

    Item {
        id: imageArea
        anchors.top: toolbar.bottom
        anchors.bottom: resultStatus.top
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true

        Image {
            id: generatedImage
            objectName: "generatedImage"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: Math.max(0, Math.min(LV.Theme.scaleMetric(242), imageArea.height))
            source: root.imageSource
            asynchronous: true
            cache: !root.showingPreview
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            clip: true
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Generated image")
            Accessible.description: root.showingPreview ? root.previewPrompt : root.result.prompt || ""

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
                            root.openImageMenu(eventPoint.position.x, eventPoint.position.y)
                    }
                    onLongPressed: root.openImageMenu(point.position.x, point.position.y)
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
            : generatedImage.status === Image.Error ? qsTr("The generated image could not be loaded.")
            : root.errorText.length > 0 ? root.errorText
            : generatedImage.status === Image.Loading ? qsTr("Loading image…")
            : root.statusText
    }
}
