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
    property var agentQuestionInbox: null
    onAgentQuestionInboxChanged: {
        if (agentQuestionInbox) agentQuestions.setSource("qrc:/iiLocalLLM/UserQuestionsSheet.qml", {inbox: agentQuestionInbox})
        else agentQuestions.source = ""
    }
    Loader { id: agentQuestions }
    property bool resultVisible: false
    property var currentResult: ({})
    property var resultJobIds: []
    property string generationRequestError: ""
    readonly property var submissionJobs: generation.jobs.filter(function(job) { return resultJobIds.indexOf(job.id) >= 0 })
    readonly property var submissionResults: generation.connected
        ? generation.completedResults.filter(function(result) { return resultJobIds.indexOf(result.id) >= 0 }) : []
    readonly property var activeGeneration: submissionJobs.find(function(job) { return job.state === "running" || job.state === "connecting-host" || job.state === "downloading" }) || ({})
    readonly property bool generationPending: submissionJobs.some(function(job) {
        return job.state === "queued" || job.state === "running" || job.state === "connecting-host" || job.state === "downloading"
    })
    onSubmissionResultsChanged: presentLatestResult()
    property int generationElapsedSeconds: 0
    readonly property string generationStatusText: {
        if (!activeGeneration.id) {
            if (generationPending) return generation.busy ? qsTr("Queued")
                : generation.inferenceStatus.state === "checking-model" ? qsTr("Checking model…")
                : generation.inferenceStatus.state === "preparing" || generation.inferenceStatus.state === "loading"
                    ? qsTr("Preparing model…") : qsTr("Queued")
            const last = submissionJobs[0]
            return last && (last.state === "cancelled" || last.state === "interrupted") ? stateLabel(last.state) : ""
        }
        const phase = generation.inferenceStatus.state
        const labels = { "preparing-model": qsTr("Preparing model for faster generation…"),
            "waiting-host": qsTr("Connecting to Society host…"),
            "remote-queued": qsTr("Queued on Society host…"),
            "remote-running": qsTr("Generating on Society host…"),
            "remote-cancelling": qsTr("Stopping generation on host…"),
            "receiving-image": qsTr("Receiving generated image…"),
            "downloading-model": qsTr("Downloading model… %1%").arg(Math.floor(100 * (generation.inferenceStatus.completedBytes || 0)
                / Math.max(1, generation.inferenceStatus.totalBytes || 1))),
            "checking-model": qsTr("Checking model… %1%").arg(Math.floor(100 * (generation.inferenceStatus.completedBytes || 0)
                / Math.max(1, generation.inferenceStatus.totalBytes || 1))),
            "loading": qsTr("Loading model…"), "encoding": qsTr("Preparing prompt…"),
            "decoding": qsTr("Rendering image…"), "cancelling": qsTr("Stopping generation…"),
            "paused": qsTr("Paused — return to Dreamscapes to continue"),
            "waiting-engine": qsTr("Waiting for image engine…") }
        const finishedSteps = generation.previewTotalSteps > 0 && generation.previewStep === generation.previewTotalSteps
        const label = phase === "cancelling" || phase === "paused" ? labels[phase]
            : phase === "decoding" || (finishedSteps && generation.inferenceStatus.backend === "native")
                ? qsTr("Rendering image…")
            : generation.previewTotalSteps > 0
                ? (generation.inferenceStatus.total > 0 && generation.inferenceStatus.total < (window.activeGeneration.steps || 0)
                    ? qsTr("Refining %1 / %2") : qsTr("Denoising %1 / %2"))
                    .arg(generation.previewStep).arg(generation.previewTotalSteps)
            : labels[phase] || (generation.previewStep > 0
            ? qsTr("Denoising %1 / %2").arg(generation.previewStep).arg(generation.previewTotalSteps)
            : qsTr("Preparing generation…"))
        return label + qsTr(" · %1 s").arg(generationElapsedSeconds)
    }
    Timer {
        interval: 1000
        repeat: true
        running: !!window.activeGeneration.id
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
    primaryColor: LV.Theme.defaultPrimary
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
        const result = submissionResults.length > 0 ? submissionResults[submissionResults.length - 1] : ({})
        const source = result.imageSource ? result.imageSource.toString() : ""
        if (source === (currentResult.imageSource ? currentResult.imageSource.toString() : ""))
            return
        currentResult = result
        if (resultVisible && source.length > 0) {
            // A completion may fill an empty input, but never overwrite a draft.
            if (quickGenerate.prompt.trim().length === 0) {
                quickGenerate.prompt = result.prompt || ""
                quickGenerate.aspectRatio = result.aspectRatio || "1:1"
            }
        }
    }
    function dismissResult() {
        resultVisible = false
        resultJobIds = []
        currentResult = ({})
        generationRequestError = ""
        resultView.resetPresentation()
    }
    function showSubmission(jobIds) {
        dismissResult()
        resultJobIds = jobIds.slice()
        quickGenerate.dismissInput()
        modelMenu.close()
        resultVisible = true
    }
    function stateLabel(state) {
        const labels = { "queued": qsTr("Queued"), "running": qsTr("Generating"), "completed": qsTr("Completed"),
            "failed": qsTr("Failed"), "cancelled": qsTr("Cancelled"), "interrupted": qsTr("Interrupted"), "downloading": qsTr("Downloading model"),
            "connecting-host": qsTr("Connecting to Society host") }
        return labels[state] || state
    }
    Component.onCompleted: {
        generation.connectStorage(initialContainerPath)
    }
    onActiveChanged: { if (active) { generation.refreshModels(); historyModel.refresh() } }
    onResultVisibleChanged: if (!resultVisible) historyModel.refresh()

    DashboardFiles {
        id: historyModel
        objectName: "generationHistoryModel"
        containerPath: generation.connected ? generation.containerPath : ""
    }
    SocietyApplication { id: societyApplication }

    GenerationController {
        id: generation
        objectName: "generationController"
        onSubmissionQueued: function(jobIds) { window.showSubmission(jobIds) }
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
            results: window.submissionResults
            previewSource: window.activeGeneration.id ? generation.previewImage : ""
            previewPrompt: window.activeGeneration.prompt || ""
            generationPending: window.generationPending
            statusText: window.generationStatusText
            generationCancellable: !!window.activeGeneration.id && generation.inferenceStatus.state !== "cancelling"
            onCancelRequested: generation.cancel(window.activeGeneration.id || "")
            errorText: window.generationRequestError || (window.generationPending ? ""
                : (window.submissionJobs.find(function(job) { return job.state === "failed" }) || {}).error || "")
            onBackRequested: {
                quickGenerate.dismissInput()
                window.dismissResult()
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
                if (generation.enqueue(prompt, aspectRatio, count).length === 0)
                    window.generationRequestError = generation.errorString
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

                GenerationHistory {
                    Layout.fillWidth: true
                    Layout.bottomMargin: LV.Theme.gap16
                    files: historyModel.generationHistory
                    loading: historyModel.loading
                    property string navigationError: ""
                    errorText: navigationError || historyModel.errorString
                    onViewAllRequested: navigationError = societyApplication.openGenerationHistory()
                        ? "" : qsTr("Could not open Society. Open Society on this device and try again.")
                }

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
                                visible: queueRow.modelData.state === "queued" || queueRow.modelData.state === "running" || queueRow.modelData.state === "connecting-host" || queueRow.modelData.state === "downloading"
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
