pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import "Views/Home"
import "Views/Result"

LV.ApplicationWindow {
    id: window
    objectName: "mainWindow"
    property string initialContainerPath: ""
    property bool resultVisible: false
    property var currentResult: ({})
    property string lastPresentedImage: ""
    readonly property bool generationPending: generation.busy || generation.jobs.some(function(job) { return job.state === "queued" })
    readonly property var activeGeneration: generation.jobs.find(function(job) { return job.state === "running" }) || ({})
    property int generationElapsedSeconds: 0
    readonly property string generationStatusText: {
        if (!generation.busy) {
            if (generationPending) return qsTr("Queued")
            const last = generation.jobs[0]
            return last && (last.state === "cancelled" || last.state === "interrupted") ? stateLabel(last.state) : ""
        }
        const phase = generation.inferenceStatus.state
        const labels = { "preparing-model": qsTr("Preparing model for faster generation…"),
            "loading": qsTr("Loading model…"), "encoding": qsTr("Preparing prompt…"),
            "decoding": qsTr("Rendering image…"), "cancelling": qsTr("Stopping generation…"),
            "waiting-engine": qsTr("Waiting for image engine…") }
        const finishedSteps = generation.previewTotalSteps > 0 && generation.previewStep === generation.previewTotalSteps
        const label = phase === "cancelling" ? labels[phase]
            : phase === "decoding" || (finishedSteps && generation.inferenceStatus.backend === "native")
                ? qsTr("Rendering image…")
            : generation.previewTotalSteps > 0
                ? qsTr("Denoising %1 / %2").arg(generation.previewStep).arg(generation.previewTotalSteps)
            : labels[phase] || (generation.previewStep > 0
            ? qsTr("Denoising %1 / %2").arg(generation.previewStep).arg(generation.previewTotalSteps)
            : qsTr("Preparing generation…"))
        return label + qsTr(" · %1 s").arg(generationElapsedSeconds)
    }
    Timer {
        interval: 1000
        repeat: true
        running: generation.busy
        triggeredOnStart: true
        onTriggered: window.generationElapsedSeconds = Math.max(0,
            Math.floor((Date.now() - Date.parse(window.activeGeneration.startedAt || new Date().toISOString())) / 1000))
    }
    // Qt 6.8 exposes QInputMethod as QObject in its QML type metadata.
    readonly property var platformInputMethod: Qt.inputMethod
    readonly property real keyboardBottomInset: platformInputMethod.visible && platformInputMethod.keyboardRectangle.height > 0
        ? Math.max(0, height - platformInputMethod.keyboardRectangle.y) : 0
    readonly property real contentTopInset: Math.max(mobileSystemSafeTopInset,
        windowChromeInteractionsEnabled && windowDragHandleEnabled && visibility !== Window.FullScreen
            ? Math.max(0, windowDragHandleTopMargin + windowDragHandleHeight) : 0)
    title: "Dreamscapes"
    width: isMobilePlatform ? 390 : 960
    height: isMobilePlatform ? 844 : 640
    desktopMinWidth: 320
    desktopMinHeight: 480
    mobileMinWidth: 320
    mobileMinHeight: 480
    transientParent: null
    visible: true
    navigationEnabled: false
    useInternalPageStack: false
    windowDragExclusionItems: [appContent]

    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)
    signal newProjectRequested(url imageSource, var generationResult)

    function presentLatestResult() {
        const result = generation.latestResult
        const source = result.imageSource ? result.imageSource.toString() : ""
        if (source === lastPresentedImage)
            return
        const enteringResult = !resultVisible
        lastPresentedImage = source
        currentResult = result
        resultVisible = source.length > 0
        if (resultVisible) {
            if (enteringResult)
                quickGenerate.dismissInput()
            modelMenu.close()
            // Reuse the current session's result without overwriting a draft.
            if (quickGenerate.prompt.trim().length === 0) {
                quickGenerate.prompt = result.prompt || ""
                quickGenerate.aspectRatio = result.aspectRatio || "1:1"
            }
        }
    }
    function stateLabel(state) {
        const labels = { "queued": qsTr("Queued"), "running": qsTr("Generating"), "completed": qsTr("Completed"),
            "failed": qsTr("Failed"), "cancelled": qsTr("Cancelled"), "interrupted": qsTr("Interrupted"), "downloading": qsTr("Receiving image") }
        return labels[state] || state
    }
    Component.onCompleted: {
        generation.connectStorage(initialContainerPath)
    }
    onActiveChanged: { if (active) generation.refreshModels() }

    GenerationController {
        id: generation
        objectName: "generationController"
        onStorageChanged: window.presentLatestResult()
        onJobsChanged: window.presentLatestResult()
    }

    Item {
        id: appContent
        objectName: "appContent"
        anchors.fill: parent
        anchors.topMargin: window.contentTopInset
        anchors.leftMargin: window.mobileSystemSafeLeftInset
        anchors.rightMargin: window.mobileSystemSafeRightInset
        anchors.bottomMargin: Math.max(window.mobileSystemSafeBottomInset, window.keyboardBottomInset)
        clip: true

        GenerationResult {
            id: resultView
            anchors.top: parent.top
            anchors.bottom: quickGenerate.top
            anchors.left: parent.left
            anchors.right: parent.right
            visible: window.resultVisible
            result: window.currentResult
            results: generation.connected ? generation.completedResults : []
            previewSource: generation.previewImage
            previewPrompt: window.activeGeneration.prompt || ""
            generationPending: window.generationPending
            statusText: window.generationStatusText
            generationCancellable: generation.busy && generation.inferenceStatus.state !== "cancelling"
            onCancelRequested: generation.cancel(window.activeGeneration.id || "")
            errorText: generation.errorString
            onBackRequested: {
                quickGenerate.dismissInput()
                window.resultVisible = false
            }
            onNewProjectRequested: function(imageSource, generationResult) {
                quickGenerate.dismissInput()
                window.newProjectRequested(imageSource, generationResult)
            }
        }

        QuickGenerate {
            id: quickGenerate
            // Switching both vertical anchors can stretch the item and discard its
            // height binding. Position it without changing its implicit height.
            y: window.resultVisible
                ? parent.height - height : 0
            anchors.left: parent.left
            anchors.right: parent.right
            height: implicitHeight
            menusOpenUpward: window.resultVisible
            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                window.generateRequested(prompt, mediaType, aspectRatio, count)
                if (generation.enqueue(prompt, aspectRatio, count).length > 0) {
                    quickGenerate.dismissInput()
                    modelMenu.close()
                    resultView.detailVisible = false
                    window.resultVisible = true
                }
            }
        }

        Flickable {
            id: storagePanel
            objectName: "storagePanel"
            visible: !window.resultVisible
            anchors.top: window.resultVisible ? parent.top : quickGenerate.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: LV.Theme.gap10
            anchors.rightMargin: LV.Theme.gap10
            anchors.bottomMargin: LV.Theme.gap10
            contentWidth: width
            contentHeight: storageContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            LV.VStack {
                id: storageContent
                width: parent.width
                height: implicitHeight
                spacing: LV.Theme.gap8

                LV.HStack {
                    Layout.fillWidth: true
                    LV.Label { text: qsTr("Society"); Layout.fillWidth: true }
                    LV.LabelButton {
                        objectName: "refreshSociety"
                        text: generation.connected ? qsTr("Refresh models") : qsTr("Check Society storage")
                        tone: LV.AbstractButton.Default
                        onClicked: generation.refreshModels()
                    }
                }
                LV.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    style: caption
                    text: generation.connected
                        ? qsTr("Models and generated images are stored in Society on this device.")
                        : qsTr("Open Society on this device and let it finish syncing your models.")
                }
                LV.LabelButton {
                    objectName: "showGenerationResultButton"
                    text: qsTr("View result")
                    tone: LV.AbstractButton.Default
                    visible: window.lastPresentedImage.length > 0 || window.generationPending
                    onClicked: window.resultVisible = true
                }
                LV.LabelMenuButton {
                    id: modelButton
                    objectName: "societyModelSelector"
                    Layout.fillWidth: true
                    text: generation.selectedModel.length ? generation.selectedModel : qsTr("Add a Diffusion model in Society")
                    tone: LV.AbstractButton.Default
                    enabled: generation.models.length > 0
                    onClicked: modelMenu.openFor(modelButton, 0, modelButton.height + LV.Theme.gap2)
                }
                LV.Label {
                    Layout.fillWidth: true
                    visible: generation.connected && !generation.runtimeAvailable
                    wrapMode: Text.Wrap
                    text: qsTr("Local image generation is unavailable in this build.")
                }
                LV.Label {
                    objectName: "generationError"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: generation.errorString
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                }
                LV.Label { text: qsTr("Generation queue"); visible: generation.jobs.length > 0 }
                Repeater {
                    model: generation.jobs
                    delegate: LV.VStack {
                        id: queueRow
                        required property var modelData
                        property bool detailsVisible: false
                        Layout.fillWidth: true
                        LV.HStack {
                            Layout.fillWidth: true
                            LV.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 3
                                text: window.stateLabel(queueRow.modelData.state) + " · " + queueRow.modelData.prompt + "\n" + queueRow.modelData.modelName
                            }
                            LV.LabelButton {
                                text: qsTr("Details")
                                tone: LV.AbstractButton.Default
                                visible: Boolean(queueRow.modelData.error)
                                onClicked: queueRow.detailsVisible = !queueRow.detailsVisible
                            }
                            LV.LabelButton {
                                text: qsTr("Cancel")
                                tone: LV.AbstractButton.Default
                                visible: queueRow.modelData.state === "queued" || queueRow.modelData.state === "running"
                                onClicked: generation.cancel(queueRow.modelData.id)
                            }
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            visible: queueRow.detailsVisible
                            text: queueRow.modelData.error || ""
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
        LV.ContextMenu {
            id: modelMenu
            objectName: "societyModelMenu"
            showIconSlot: false
            itemWidth: Math.max(0, Math.min(440, storagePanel.width - leftPadding - rightPadding - edgeMargin * 2))
            items: generation.models.map(function(model) { return model.id })
            selectedIndex: items.indexOf(generation.selectedModel)
            onItemTriggered: function(index, entry) { generation.selectedModel = String(entry) }
        }
    }
}
