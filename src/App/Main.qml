pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.platform as Platform
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import "Views/Home"
import "Views/Result"
import "Views/Editor"
import "Views/Publishing"
import "Views/Preferences"
import "Views"
import "Views/Home/ImageSelection.js" as ImageSelection
import "Views/Home/StorageBrowserEntries.js" as StorageEntries

LV.ApplicationWindow {
    id: window
    objectName: "mainWindow"
    property var preferencesWindow: null
    property string dismissedFailureId: ""
    function showFailureExplanation(id) {
        failureDetails.report = generationBackend.failureAnalysis(id)
        failureDetails.open = true
    }
    FailureDetails {
        id: failureDetails
        onPrimaryClicked: { open = false; window.dismissedFailureId = report.jobId || "" }
    }
    Connections {
        target: window.generationBackend
        function onFailureExplained(jobId, report) {
            if (failureDetails.report.jobId === jobId) failureDetails.report = report
        }
    }
    LV.LabelButton {
        objectName: "failureExplanationButton"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 8
        z: 900
        visible: !!window.generationBackend.latestFailure.jobId && window.dismissedFailureId !== window.generationBackend.latestFailure.jobId
        text: window.generationBackend.latestFailure.state === "pending" ? qsTr("Failure details · Analyzing…") : qsTr("Failure details")
        tone: LV.AbstractButton.Default
        onClicked: window.showFailureExplanation(window.generationBackend.latestFailure.jobId)
    }
    function openPreferences() {
        if (isMobilePlatform)
            return;
        if (!preferencesWindow)
            preferencesWindow = preferencesComponent.createObject(window);
        preferencesWindow.showNormal();
        preferencesWindow.raise();
        preferencesWindow.requestActivate();
    }
    onClosing: if (preferencesWindow) preferencesWindow.close()
    Component {
        id: preferencesComponent
        PreferencesWindow {
            transientParent: window
            generation: window.generationBackend
            account: window.generationBackend.account
            onSocietyRequested: window.openSociety()
        }
    }
    Shortcut {
        objectName: "preferencesShortcut"
        sequence: "Ctrl+,"
        context: Qt.ApplicationShortcut
        enabled: !window.isMobilePlatform
        onActivated: window.openPreferences()
    }
    Platform.MenuBar {
        objectName: "globalMenuBar"
        window: window
        Platform.Menu {
            title: qsTr("File")
            Platform.MenuItem {
                text: qsTr("Quit Dreamscapes")
                role: Platform.MenuItem.QuitRole
                onTriggered: Qt.quit()
            }
        }
        Platform.Menu {
            objectName: "globalEditMenu"
            title: qsTr("Edit")
            Platform.MenuItem {
                objectName: "globalPreferencesAction"
                text: qsTr("Preferences…")
                role: Platform.MenuItem.NoRole
                shortcut: "Ctrl+,"
                onTriggered: window.openPreferences()
            }
        }
        Platform.Menu { title: qsTr("Window") }
        Platform.Menu {
            title: qsTr("Help")
            Platform.MenuItem {
                text: qsTr("Latest failure explanation…")
                enabled: !!window.generationBackend.latestFailure.jobId
                onTriggered: window.showFailureExplanation(window.generationBackend.latestFailure.jobId)
            }
        }
    }
    property string initialContainerPath: ""
    // Theme.targetOverride is the LVRS-supported preview/test hook; the real
    // iOS and Android builds still enter this path through isMobilePlatform.
    readonly property bool useMobileHomeLayout: isMobilePlatform
        || LV.Theme.effectiveTarget === "ios" || LV.Theme.effectiveTarget === "android"
    property var agentQuestionInbox: null
    onAgentQuestionInboxChanged: {
        if (agentQuestionInbox) agentQuestions.setSource("qrc:/iiLocalLLM/UserQuestionsSheet.qml", {inbox: agentQuestionInbox})
        else agentQuestions.source = ""
    }
    Loader { id: agentQuestions }
    property bool resultVisible: false
    property bool editorVisible: false
    property bool publishVisible: false
    readonly property var publishView: publishLoader.item
    function openPublish() {
        if (!editorVisible || !editorView || editorView.modalActive || editorView.documentOpening || editorView.imageImportRequest.active) return false
        publishLoader.active = true
        if (!publishLoader.item) return false
        editorLoader.retainedWidth = editorLoader.width
        editorLoader.retainedHeight = editorLoader.height
        publishVisible = true
        publishLoader.item.open()
        publishLoader.item.forceActiveFocus()
        return true
    }
    function closePublish() {
        publishVisible = false
        if (editorView) editorView.blankCanvas.forceActiveFocus()
    }
    onResultVisibleChanged: cancelFileOpen()
    onEditorVisibleChanged: { cancelFileOpen(); if (!editorVisible) publishVisible = false }
    property string storageBrowserSection: ""
    property string pendingFileKey: ""
    property string pendingFileSection: ""
    property string pendingFileContainerId: ""
    property var pendingCanvasOpen: null
    readonly property bool storageBrowserVisible: storageBrowserSection.length > 0 && !resultVisible && !editorVisible
    onStorageBrowserVisibleChanged: if (!storageBrowserVisible) cancelFileOpen()
    onStorageBrowserSectionChanged: if (imageOpen.active || (editorView && (editorView.imageImportRequest.active || editorView.documentOpening)) || (pendingFileSection && pendingFileSection !== storageBrowserSection)) cancelFileOpen()
    readonly property var editorView: editorLoader.item
    function ensureEditor() {
        editorLoader.active = true
        return editorLoader.item
    }
    property url canvasImageSource: ""
    property var canvasSpecification: ({})
    property var canvasGenerationResult: ({})
    property var canvasGenerationResults: []
    property var currentResult: ({})
    property var resultJobIds: []
    property string generationRequestError: ""
    readonly property var generationHistoryEntries: ImageSelection.historyImages(historyModel.generationHistory).map(function(file) {
        if (/\.iisc$/i.test(file.name || "")) return generation.storedImage(file)
        if (!/^[0-9a-f-]{36}-poster\.png$/.test(file.name || "")) return file
        const video = Object.assign({},file)
        video.mediaType = "Video"
        video.imageSource = file.previewSource
        video.mediaSource = file.previewSource.toString().replace(/-poster\.png(\?.*)?$/, "-0001.mp4")
        video.name = file.name.replace(/-poster\.png$/, "-0001.mp4")
        video.metadata1 = "MP4 · Video"
        return video
    })
    readonly property alias generationBackend: generation
    readonly property var submissionJobs: generation.jobs.filter(function(job) { return resultJobIds.indexOf(job.id) >= 0 })
    readonly property var submissionResults: generation.connected
        ? generation.completedResults.filter(function(result) { return resultJobIds.indexOf(result.id) >= 0 }) : []
    readonly property var reviewResults: {
        const entries = generation.connected ? generation.completedResults : []
        if (resultJobIds.length || !currentResult.imageSource) return entries
        const source = (currentResult.documentSource || currentResult.mediaSource || currentResult.imageSource).toString()
        return entries.some(entry => (entry.documentSource || entry.mediaSource || entry.imageSource || "").toString() === source)
            ? entries : entries.concat([currentResult])
    }
    readonly property var reviewActiveJob: generation.jobs.find(job => ["running","connecting-host","downloading"].indexOf(job.state) >= 0) || ({})
    readonly property bool reviewPending: generation.jobs.some(job => ["queued","running","connecting-host","downloading"].indexOf(job.state) >= 0)
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
        const labels = { "interpolating": qsTr("Interpolating video frames…"), "encoding-video": qsTr("Encoding video…"), "complete": qsTr("Saving video…"), "preparing-model": qsTr("Preparing model for faster generation…"),
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
            "decoding": window.activeGeneration.mediaType === "Video" ? qsTr("Rendering video frames…") : qsTr("Rendering image…"), "cancelling": qsTr("Stopping generation…"),
            "paused": qsTr("Paused — return to Dreamscapes to continue"),
            "waiting-engine": qsTr("Waiting for image engine…") }
        const finishedSteps = generation.previewTotalSteps > 0 && generation.previewStep === generation.previewTotalSteps
        const label = phase === "cancelling" || phase === "paused" ? labels[phase]
            : phase === "decoding" || (finishedSteps && generation.inferenceStatus.backend === "native")
                ? labels.decoding
            : window.activeGeneration.mediaType === "Video" && (phase === "interpolating" || phase === "encoding-video" || phase === "complete") ? labels[phase]
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
    // One application title bar keeps Home and Editor navigation geometrically stable.
    readonly property real desktopTitleBarHeight: LV.Theme.scaleMetric(32)
    readonly property real desktopTitleBarContentGap: 0
    readonly property real desktopTitleBarLeadingClearance: nativeTitleBarControlsRect.width > 0
        ? nativeTitleBarControlsRect.x + nativeTitleBarControlsRect.width + LV.Theme.gap12
        : LV.Theme.gap12
    readonly property real contentTopInset: Math.max(mobileSystemSafeTopInset,
        windowChromeInteractionsEnabled && windowDragHandleEnabled && visibility !== Window.FullScreen
            ? Math.max(0, windowDragHandleTopMargin + windowDragHandleHeight) : 0,
        !useMobileHomeLayout
            ? (visibility === Window.FullScreen
                ? LV.Theme.controlHeightSm + LV.Theme.gap12
                : desktopTitleBarHeight + desktopTitleBarContentGap)
            : 0)
    title: "Dreamscapes"
    primaryColor: LV.Theme.defaultPrimary
    width: useMobileHomeLayout ? 390 : 1920
    height: useMobileHomeLayout ? 844 : 1200
    desktopMinWidth: 320
    desktopMinHeight: 480
    mobileMinWidth: 320
    mobileMinHeight: 480
    transientParent: null
    visible: true
    navigationEnabled: false
    useInternalPageStack: false
    nativeTitleBarHeight: useMobileHomeLayout ? 0 : desktopTitleBarHeight
    nativeTitleBarLeftMargin: LV.Theme.gap16
    // LVRS owns window movement only in this top strip; content drags paint/select.
    windowDragHandleHeight: 32
    windowDragExclusionItems: [appContent, desktopToolbar]

    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)

    function openCanvas(imageSource, generationResult) {
        cancelFileOpen()
        ensureEditor()
        imageSource = generationResult && generationResult.documentSource || imageSource
        quickGenerate.dismissInput()
        modelMenu.close()
        if (imageSource) {
            const intent = { source: imageSource.toString(), result: generationResult || ({}) }
            pendingCanvasOpen = intent
            if (!editorView.openDocumentSource(imageSource)) {
                if (pendingCanvasOpen === intent) pendingCanvasOpen = null
                generationRequestError = editorView.documentError
                return false
            }
            generationRequestError = ""
            return true
        }
        if (!editorView.createBlankCanvas({width: 1024, height: 1024, unit: "px", ppi: 300, background: "White", name: qsTr("Untitled Canvas")})) {
            generationRequestError = editorView.documentError
            return false
        }
        return finishCanvasOpen("", generationResult)
    }
    function finishCanvasOpen(imageSource, generationResult) {
        publishVisible = false
        canvasImageSource = imageSource || ""
        canvasGenerationResult = generationResult || ({})
        canvasGenerationResults = generationResult && imageSource ? [generationResult] : []
        canvasSpecification = ({})
        generationRequestError = ""
        editorVisible = true
        editorView.forceActiveFocus()
        return true
    }
    function openImages(entries) {
        if (!entries || !entries.length) return false
        const images = entries.filter(entry => ImageSelection.isImage(entry))
        if (images.length !== entries.length) {
            generationRequestError = qsTr("Select images to open in the editor.")
            return false
        }
        cancelFileOpen()
        const unavailable = images.find(entry => entry.available === false)
        if (unavailable && unavailable.section && (!storageBrowserVisible || storageBrowserSection !== unavailable.section))
            showStorage(unavailable.section)
        if (!imageOpen.start(images)) return false
        pendingFileKey = ""
        pendingFileSection = ""
        pendingFileContainerId = ""
        generationRequestError = ""
        return true
    }
    ImageOpenRequest {
        id: imageOpen
        objectName: "imageOpenRequest"
        container: window.societyContainer
        download: generation.fileDownload
        onReady: function(images, sources) { window.openPreparedImages(images, sources) }
        onChanged: if (active) window.generationRequestError = ""
        onFailed: function(message) { window.generationRequestError = message; generation.explainOperationFailure("Open canvas", message) }
        onRejected: function(message) { window.generationRequestError = message }
    }
    function openPreparedImages(images, sources) {
        resultView.rememberGalleryPosition()
        ensureEditor()
        // Rejection is delivered by the command's signal; its existing operation
        // and document error states deliberately remain independent.
        if (!editorView.openImages(sources, images)) return false
        generationRequestError = ""
        return true
    }
    function finishPreparedImages(images, sources) {
        quickGenerate.dismissInput()
        modelMenu.close()
        canvasImageSource = sources[0]
        canvasGenerationResult = images[0] || ({})
        canvasGenerationResults = images.slice()
        canvasSpecification = ({})
        generationRequestError = ""
        editorVisible = true
        editorView.forceActiveFocus()
        return true
    }
    function openHomeFileDialog() {
        cancelFileOpen()
        quickGenerate.dismissInput()
        modelMenu.close()
        homeOpenDialog.open()
    }
    function closeCanvas() {
        cancelFileOpen()
        editorVisible = false
    }
    function openNewCanvas() {
        cancelFileOpen()
        quickGenerate.dismissInput()
        modelMenu.close()
        newCanvasDialog.begin()
    }

    function presentLatestResult() {
        // Manual file views have no submission owner. Unrelated job updates
        // must never replace the file the user explicitly opened.
        if (!resultJobIds.length) return
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
    function cancelSubmission() {
        const pendingJobs = submissionJobs.filter(job => ["queued", "running", "connecting-host", "downloading"].indexOf(job.state) >= 0)
        // Retire the queued tail before an active worker can release the queue.
        pendingJobs.filter(job => job.state === "queued").forEach(job => generation.cancel(job.id))
        pendingJobs.filter(job => job.state !== "queued").forEach(job => generation.cancel(job.id))
        quickGenerate.dismissInput()
        dismissResult()
        desktopHome.showHome()
        mobileHome.showHome()
    }
    function showSubmission(jobIds) {
        cancelFileOpen()
        storageBrowserSection = ""
        editorVisible = false
        resultView.resetPresentation()
        generationRequestError = ""
        currentResult = ({})
        resultJobIds = jobIds.slice()
        quickGenerate.dismissInput()
        modelMenu.close()
        resultVisible = true
        Qt.callLater(resultView.focusResults)
    }
    function openGenerationReview() {
        cancelFileOpen()
        storageBrowserSection = ""
        editorVisible = false
        quickGenerate.dismissInput()
        modelMenu.close()
        resultVisible = true
        Qt.callLater(resultView.focusResults)
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
    CanvasPreview {
        // Generation result URLs share the existing asynchronous IISC provider.
        Component.onCompleted: sourceForFile({})
    }
    // Native inventory/file monitors publish real backend changes. Window focus
    // and result visibility are presentation and never reopen/rescan storage.

    // Helper owns the container mirror and supplies Alias metadata and previews.
    readonly property var societyContainer: generation.container
    QtObject {
        id: historyModel
        objectName: "generationHistoryModel"
        readonly property var snapshot: window.societyContainer
        readonly property string containerPath: snapshot.rootPath
        readonly property var recentFiles: snapshot.recentFiles
        readonly property var recentPublished: snapshot.recentPublished
        readonly property var generationHistory: snapshot.generationHistory
        readonly property bool loading: snapshot.loading
        readonly property string errorString: snapshot.errorString
        property string query: window.useMobileHomeLayout ? "" : desktopToolbar.query
        onQueryChanged: snapshot.query = query
        function refresh() { snapshot.refresh() }
    }
    readonly property alias socialPublishing: publishing
    SocialPublishingController {
        id: publishing
        objectName: "socialPublishingController"
        onJobFinished: function(jobId, result) {
            if (!result.published && result.error !== "cancelled")
                generation.explainOperationFailure("Publish", result.message || result.error, {
                    providerJobId: jobId, errorCode: result.error,
                    completedSteps: (result.completed || []).length, nextStep: result.next,
                    requiresReconciliation: result.requiresReconciliation
                })
        }
    }
    GenerationController {
        id: generation
        objectName: "generationController"
        onSubmissionQueued: function(jobIds) {
            // Editor operations retain their target canvas and inline result flow.
            if (window.editorView && window.editorView.submitting) return
            // Home and the Image/Video generation views share one review route.
            window.showSubmission(jobIds)
        }
    }

    // Home and Editor share the same application controls and title-bar geometry.
    DesktopHomeToolbar {
        id: desktopToolbar
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Math.max(0, ((window.visibility === Window.FullScreen
            ? LV.Theme.controlHeightSm + LV.Theme.gap12 : window.desktopTitleBarHeight) - height) / 2)
        anchors.rightMargin: LV.Theme.gap12
        width: Math.min(LV.Theme.scaleMetric(265), Math.max(0,
            window.width - anchors.rightMargin - window.desktopTitleBarLeadingClearance))
        visible: !window.useMobileHomeLayout && !window.storageBrowserVisible
        jobs: generation.jobs
        inlineSearch: window.editorVisible && !window.publishVisible
        searchResults: historyModel.recentFiles.map(file => ({file: file, label: qsTr("Files") + " · " + file.name}))
            .concat(historyModel.recentPublished.map(file => ({file: file, label: qsTr("Published") + " · " + file.name})))
            .concat(window.generationHistoryEntries.map(file => ({file: file, label: qsTr("History") + " · " + file.name}))).slice(0, 8)
        onPreferencesRequested: window.openPreferences()
        onSocietyRequested: window.openSociety()
        onActivityRequested: if (!window.editorVisible) desktopHome.showHome()
        onSearchDismissed: if (window.editorVisible && window.editorView) window.editorView.blankCanvas.forceActiveFocus()
        onFileRequested: function(file) { window.openHomeFile(file) }
    }
    Shortcut {
        sequence: StandardKey.Find
        enabled: (desktopToolbar.visible || window.storageBrowserVisible) && !newCanvasDialog.visible
            && !window.publishVisible
            && !(window.editorVisible && window.editorView && window.editorView.modalActive)
        onActivated: window.storageBrowserVisible ? storageBrowserLoader.item.focusSearch() : desktopToolbar.focusSearch()
    }
    Shortcut { sequence: StandardKey.New; enabled: !newCanvasDialog.visible; onActivated: window.openNewCanvas() }
    Shortcut {
        sequence: StandardKey.Open
        enabled: !window.editorVisible && !newCanvasDialog.visible && !homeOpenDialog.visible
        onActivated: window.openHomeFileDialog()
    }
    FileDialog {
        id: homeOpenDialog
        objectName: "homeOpenFileDialog"
        title: qsTr("Open Canvas or Image")
        nameFilters: [qsTr("Projects, canvases and images (*.iiscp *.iisc *.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)")]
        onAccepted: window.openCanvas(selectedFile, ({}))
        onVisibleChanged: if (!visible && window.editorVisible) Qt.callLater(() => editorView.forceActiveFocus())
    }
    NewCanvasDialog {
        id: newCanvasDialog
        availableArea: appContent
        onCanvasRequested: function(specification) {
            window.ensureEditor()
            if (!editorView.createBlankCanvas(specification)) {
                creationError = qsTr("Could not create the canvas. Adjust its dimensions and try again.")
                return
            }
            window.canvasSpecification = specification
            window.canvasImageSource = ""
            window.canvasGenerationResult = ({})
            window.canvasGenerationResults = []
            window.generationRequestError = ""
            window.editorVisible = true
            close()
            editorView.forceActiveFocus()
        }
    }
    function openSociety() {
        window.generationRequestError = Qt.openUrlExternally("society://")
            ? "" : qsTr("Could not open Society. Open Society on this device and try again.")
    }
    function reportUnavailableAction(action) {
        generationRequestError = action === "audio"
            ? qsTr("Audio generation is not available yet. Your prompt is preserved.")
            : action === "board" ? qsTr("Board creation is not available yet. Your prompt is preserved.")
            : qsTr("This action is not available on this device.")
        return false
    }
    function openHomeFile(file) {
        imageOpen.cancel()
        if (!file || typeof file !== "object") {
            generationRequestError = qsTr("Choose a file to open.")
            return false
        }
        if (file.exists === false) {
            generationRequestError = qsTr("This file is no longer available.")
            return false
        }
        if (file.available === false) {
            const section = file.section || "files"
            if (!file.key || !file.containerId) {
                generationRequestError = qsTr("The file's drive information is missing. Refresh files and try again.")
                return false
            }
            if (!storageBrowserVisible || storageBrowserSection !== section) showStorage(section)
            pendingFileKey = file.key
            pendingFileSection = section
            pendingFileContainerId = file.containerId
            generationRequestError = ""
            if (generation.fileDownload.start(file.key, file.containerId)) return true
            pendingFileKey = ""
            pendingFileSection = ""
            pendingFileContainerId = ""
            generationRequestError = qsTr("The file download could not be started. Refresh files and try again.")
            return false
        }
        cancelFileOpen()
        if (file.mediaType === "Video") {
            editorVisible = false
            dismissResult()
            currentResult = file
            resultVisible = true
            Qt.callLater(function() { resultView.showEntry(file) })
            return true
        }
        const name = String(file.name || file.path || "")
        if (/\.iiscp?$/i.test(name) || file.isImage === true || (file.isImage === undefined && ImageSelection.isImage(file))) {
            const source = file.source || (file.path ? window.ensureEditor().localFileSource(String(file.path)) : ImageSelection.source(file))
            return window.openCanvas(source, file)
        }
        if (file.source && Qt.openUrlExternally(file.source)) return true
        generationRequestError = qsTr("This file could not be opened on this device.")
        return false
    }
    function cancelFileOpen() {
        const key = pendingFileKey
        const containerId = pendingFileContainerId
        imageOpen.cancel()
        if (editorView) { editorView.imageImportRequest.cancel(); editorView.cancelDocumentOpen() }
        pendingCanvasOpen = null
        ImageSelection.cancelOwnedDownload(generation.fileDownload, key, containerId)
        pendingFileKey = ""
        pendingFileSection = ""
        pendingFileContainerId = ""
    }
    Connections {
        target: generation.fileDownload
        function onReady(file) {
            if (!window.storageBrowserVisible || window.pendingFileKey !== file.key || window.pendingFileContainerId !== file.containerId
                || window.societyContainer.containerId !== file.containerId
                || window.pendingFileSection !== window.storageBrowserSection) return
            const fresh = StorageEntries.sectionFiles(window.societyContainer.aliases, window.pendingFileSection)
                .find(entry => entry.key === file.key)
            window.pendingFileKey = ""
            window.pendingFileSection = ""
            window.pendingFileContainerId = ""
            if (fresh && fresh.available) window.openHomeFile(fresh)
        }
    }
    function showStorage(section) {
        if (["files", "published", "generation-history"].indexOf(section) < 0) return
        cancelFileOpen()
        quickGenerate.dismissInput()
        modelMenu.close()
        editorVisible = false
        resultVisible = false
        generationRequestError = ""
        storageBrowserSection = section
        storageBrowserLoader.opened = true
        desktopHome.selectedAction = section === "generation-history" ? "history" : section === "published" ? "published" : "files"
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

        MobileHome {
            id: mobileHome
            anchors.fill: parent
            visible: window.useMobileHomeLayout && !window.resultVisible && !window.editorVisible && !window.storageBrowserVisible
            onQuickActionRequested: function(action) {
                if (action === "canvas") window.openNewCanvas()
                else if (action === "open") window.openHomeFileDialog()
                else if (action === "image") { quickGenerate.mediaType = "Image"; quickGenerate.focusPrompt() }
                else if (action === "video") {
                    quickGenerate.mediaType = "Video"
                    quickGenerate.focusPrompt()
                    window.generationRequestError = ""
                } else window.reportUnavailableAction(action)
            }
            recentFiles: historyModel.recentFiles
            recentPublished: historyModel.recentPublished
            generationHistory: window.generationHistoryEntries
            loading: historyModel.loading
            errorText: window.generationRequestError || historyModel.errorString
            onFileRequested: function(file) { window.openHomeFile(file) }
            onImagesRequested: function(images) { window.openImages(images) }
            onViewAllRecentFilesRequested: window.showStorage("files")
            onViewAllPublishedRequested: window.showStorage("published")
            onSearchRequested: { window.showStorage("files"); Qt.callLater(() => storageBrowserLoader.item.focusSearch()) }
            onDestinationRequested: function(index) {
                if (index === 0) mobileHome.showHome()
                else if (index === 1) window.openCanvas("", ({}))
                else if (index === 2) window.showStorage("files")
                else if (index === 3) window.showStorage("generation-history")
                else if (index === 4) window.openSociety()
            }
            onViewAllGenerationHistoryRequested: window.showStorage("generation-history")
        }

        DesktopHome {
            id: desktopHome
            anchors.fill: parent
            visible: !window.useMobileHomeLayout && !window.resultVisible && !window.editorVisible
            recentFiles: historyModel.recentFiles
            recentPublished: historyModel.recentPublished
            generationHistory: window.generationHistoryEntries
            jobs: generation.jobs
            generationStatus: window.generationStatusText
            loading: historyModel.loading
            errorText: window.generationRequestError || historyModel.errorString
            browserSection: window.storageBrowserSection
            onHomeRequested: window.storageBrowserSection = ""
            onQueueRequested: window.openGenerationReview()
            onQuickActionRequested: function(action) {
                window.storageBrowserSection = ""
                if (action === "canvas") window.openNewCanvas()
                else if (action === "tools") window.openCanvas("", ({}))
                else if (action === "image") {
                    imageWorkspaceLoader.opened = true
                    if (imageWorkspaceLoader.item) imageWorkspaceLoader.item.parameterPanel.focusPrompt()
                    window.generationRequestError = ""
                } else if (action === "video") {
                    quickGenerate.dismissInput()
                    videoWorkspaceLoader.openWorkspace()
                    if (videoWorkspaceLoader.item) videoWorkspaceLoader.item.parameterPanel.focusPrompt()
                    window.generationRequestError = ""
                } else {
                    window.reportUnavailableAction(action)
                }
            }
            onBrowseRequested: window.showStorage("files")
            onPublishedRequested: window.showStorage("published")
            onHistoryRequested: window.showStorage("generation-history")
            onFileRequested: function(file) { window.openHomeFile(file) }
            onOpenFileRequested: window.openHomeFileDialog()
            onImagesRequested: function(images) { window.openImages(images) }
            onPromptRequested: function(prompt) { quickGenerate.prompt = prompt; quickGenerate.focusPrompt() }
        }

        Loader {
            id: storageBrowserLoader
            objectName: "storageBrowserLoader"
            property bool opened: false
            active: opened
            parent: window.useMobileHomeLayout ? appContent : desktopHome.storageBrowserContainer
            anchors.fill: parent
            visible: window.storageBrowserVisible
            sourceComponent: StorageBrowser {
                container: window.societyContainer
                download: generation.fileDownload
                imageOpenRequest: imageOpen
                imageImporter: window.editorView ? window.editorView.imageImportRequest : null
                section: window.storageBrowserSection || "files"
                errorText: window.generationRequestError
                onBackRequested: {
                    window.storageBrowserSection = ""
                    desktopHome.showHome()
                    mobileHome.showHome()
                }
                onSectionRequested: function(section) { window.showStorage(section) }
                onFileRequested: function(file) { window.openHomeFile(file) }
                onImagesRequested: function(images) { window.openImages(images) }
                onOpenFileRequested: window.openHomeFileDialog()
            }
        }

        Loader {
            id: imageWorkspaceLoader
            objectName: "imageGenerationWorkspaceLoader"
            property bool opened: false
            // Retain the draft after first use, without inflating Home startup.
            active: opened
            parent: desktopHome.imageWorkspaceContainer
            anchors.fill: parent
            visible: !window.useMobileHomeLayout && desktopHome.selectedAction === "image"
            sourceComponent: ImageGenerationWorkspace {
                generation: window.generationBackend
                onEditImageRequested: function(source, result) { window.openCanvas(source, result) }
                onEditImagesRequested: function(images) { window.openImages(images) }
            }
        }

        Loader {
            id: videoWorkspaceLoader
            objectName: "videoGenerationWorkspaceLoader"
            property bool opened: false
            active: opened
            parent: desktopHome.videoWorkspaceContainer
            anchors.fill: parent
            visible: !window.useMobileHomeLayout && desktopHome.selectedAction === "video"
            function openWorkspace() {
                if (!opened) {
                    setSource(Qt.resolvedUrl("Views/Video/VideoGenerationWorkspace.qml"), {generation: window.generationBackend})
                    opened = true
                }
            }
        }

        GenerationResult {
            id: resultView
            onOperationFailed: function(operation, message, parameters) { generation.explainOperationFailure(operation, message, parameters) }
            anchors.top: parent.top
            anchors.bottom: window.resultVisible
                ? resultComposerViewport.top : parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            visible: window.resultVisible && !window.editorVisible
            result: window.currentResult
            results: window.reviewResults
            jobs: generation.jobs
            inferenceStatus: generation.inferenceStatus
            previewSource: window.reviewActiveJob.id && generation.previewJobId === window.reviewActiveJob.id ? generation.previewImage : ""
            previewPrompt: window.reviewActiveJob.prompt || ""
            generationPending: window.reviewPending
            cancelAvailable: window.generationPending
            statusText: window.generationStatusText
            onCancelRequested: window.cancelSubmission()
            onCancelJobRequested: function(id) { generation.cancel(id) }
            onFailureRequested: function(id) { window.showFailureExplanation(id) }
            errorText: window.generationRequestError || (window.generationPending ? ""
                : (window.submissionJobs.find(function(job) { return job.state === "failed" }) || {}).error || "")
            onBackRequested: {
                quickGenerate.dismissInput()
                window.dismissResult()
            }
            onImagesRequested: function(images) { window.openImages(images) }
        }

        Loader {
            id: editorLoader
            objectName: "canvasEditorLoader"
            property real retainedWidth: 0
            property real retainedHeight: 0
            active: false
            anchors.top: parent.top
            anchors.left: parent.left
            width: window.publishVisible ? retainedWidth : parent.width
            height: window.publishVisible ? retainedHeight : parent.height
            visible: window.editorVisible && !window.publishVisible
            sourceComponent: CanvasEditor {
                generationRuntime: generation
                applicationModalActive: desktopToolbar.modalActive || window.publishVisible
                applicationSearchActive: desktopToolbar.searchActive
                mobileLayout: window.useMobileHomeLayout
                visible: window.editorVisible
                imageSource: window.canvasImageSource
                initializeBlankCanvas: false
                generationResult: window.canvasGenerationResult
                generationResults: window.canvasGenerationResults
                canvasSpecification: window.canvasSpecification
                onImagesImported: function(images, sources, attached) { if (!attached) window.finishPreparedImages(images, sources) }
                onImageImportFailed: function(message) { window.generationRequestError = message }
                onDocumentOpened: function(source) {
                    const intent = window.pendingCanvasOpen
                    const result = intent && intent.source === source.toString() ? intent.result : ({})
                    window.pendingCanvasOpen = null
                    window.finishCanvasOpen(source, result)
                }
                onDocumentOpenFailed: function(message) { window.pendingCanvasOpen = null; window.generationRequestError = message }
                onDocumentOpenCancelled: window.pendingCanvasOpen = null
                onBackRequested: window.closeCanvas()
                onPublishRequested: window.openPublish()
                onNavigationRequested: function(action) {
                    window.closeCanvas()
                    desktopHome.activate(action)
                }
            }
        }

        Loader {
            id: publishLoader
            objectName: "publishWorkspaceLoader"
            active: false
            anchors.fill: parent
            visible: window.publishVisible && window.editorVisible
            sourceComponent: PublishWorkspace {
                project: window.editorView.project
                backend: publishing
                onBackRequested: window.closePublish()
            }
        }

        LV.HStack {
            objectName: "imageImportStatus"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: LV.Theme.gap16
            spacing: LV.Theme.gap8
            z: 20
            visible: !!window.editorView && (window.editorView.documentOpening
                || (!window.editorVisible && !window.storageBrowserVisible && window.editorView.imageImportRequest.active))
            LV.Label { text: window.editorView && window.editorView.documentOpening ? qsTr("Opening document…") : qsTr("Opening originals…"); style: body }
            LV.LabelButton {
                objectName: "imageImportCancel"
                text: qsTr("Cancel")
                tone: LV.AbstractButton.Default
                onClicked: if (window.editorView.documentOpening) window.editorView.cancelDocumentOpen(); else window.editorView.imageImportRequest.cancel()
            }
        }
        Flickable {
            id: resultComposerViewport
            objectName: "resultComposerViewport"
            property real retainedComposerHeight: LV.Theme.controlHeightSm * 2 + LV.Theme.gap8 + LV.Theme.gap10 * 2
            function retainComposerHeight() {
                if (visible) retainedComposerHeight = quickGenerate.implicitHeight
            }
            onVisibleChanged: if (visible) Qt.callLater(retainComposerHeight)
            Connections {
                target: quickGenerate
                function onImplicitHeightChanged() { Qt.callLater(resultComposerViewport.retainComposerHeight) }
            }
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: window.resultVisible && !window.editorVisible
            // Preserve access to Cancel when a prompt grows beyond the window.
            // The shared composer moves to Home while Result is hidden. Keep
            // Result's geometry so its gallery does not clamp its scroll offset.
            height: Math.min(retainedComposerHeight,
                Math.max(0, parent.height - LV.Theme.controlHeightSm * 2))
            contentWidth: width
            contentHeight: retainedComposerHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        }

        QuickGenerate {
            id: quickGenerate
            visible: !window.editorVisible && !window.storageBrowserVisible && (window.resultVisible || window.useMobileHomeLayout || desktopHome.selectedAction !== "image")
            parent: window.resultVisible ? resultComposerViewport.contentItem : window.useMobileHomeLayout
                ? mobileHome.quickGenerateContainer : desktopHome.quickGenerateContainer
            y: 0
            x: 0
            width: parent ? parent.width : 0
            height: implicitHeight
            contentInset: window.resultVisible ? LV.Theme.gap10 : 0
            canvasEnabled: !window.useMobileHomeLayout && !window.resultVisible
            desktopLayout: !window.useMobileHomeLayout && !window.resultVisible
            generation: window.generationBackend
            menusOpenUpward: window.resultVisible
            generationPending: window.generationPending
            defaultPrompts: generation.defaultGenerationPrompts
            videoModels: generation.videoModels
            selectedVideoModel: generation.selectedVideoModel
            onVideoModelSelected: function(id) { generation.selectedVideoModel = id }
            onPaintRequested: window.openNewCanvas()
            onMediaActionRequested: function(action) {
                window.reportUnavailableAction(action)
            }
            onAdvancedRequested: function(parameters) {
                if (!Object.keys(parameters).length) {
                    window.generationRequestError = quickGenerate.homePaint.canvas.inputError
                    return
                }
                quickGenerate.dismissInput()
                if (quickGenerate.mediaType === "Video") {
                    videoWorkspaceLoader.openWorkspace()
                    const panel = videoWorkspaceLoader.item.parameterPanel
                    for (const key of ["prompt", "negativePrompt", "width", "height", "outputCount", "seed"])
                        panel.edit(key, parameters[key])
                    panel.edit("randomSeed", parameters.seed < 0)
                    panel.edit("duration", quickGenerate.videoDuration)
                    panel.edit("fps", quickGenerate.videoFps)
                    if (parameters.referenceImages.length) panel.addImageKey(parameters.referenceImages[0], 0)
                    desktopHome.selectedAction = "video"
                    panel.focusPrompt()
                } else {
                    imageWorkspaceLoader.opened = true
                    const panel = imageWorkspaceLoader.item.parameterPanel
                    if (!panel.parameterStore.updateParameters(parameters)) {
                        window.generationRequestError = panel.parameterStore.errorString
                        return
                    }
                    desktopHome.selectedAction = "image"
                    panel.focusPrompt()
                }
                window.generationRequestError = ""
            }
            errorText: window.resultVisible ? "" : window.generationRequestError
            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                const draft = quickGenerate.desktopLayout ? quickGenerate.desktopComposer : null
                const seed = draft ? draft.seed : -1
                window.generateRequested(prompt, mediaType, aspectRatio, count)
                window.generationRequestError = ""
                let jobId
                if (mediaType === "Video") {
                    let firstFrame = ""
                    if (quickGenerate.hasCanvasInputs) {
                        const parameters = quickGenerate.generationParameters(generation.selectedVideoModel)
                        const references = parameters.referenceImages || []
                        if (Object.keys(parameters).length === 0 || references.length !== 1) {
                            window.generationRequestError = quickGenerate.homePaint.canvas.inputError
                                || qsTr("Use one image or one painted canvas as the video's first frame.")
                            return
                        }
                        firstFrame = references[0]
                    }
                    if (draft && draft.negativePrompt !== draft.appliedNegativePrompt) {
                        jobId = generation.enqueueVideoRecipe({prompt: prompt, negativePrompt: draft.negativePrompt,
                            width: draft.outputSize.width, height: draft.outputSize.height, outputCount: count, seed: seed,
                            duration: quickGenerate.videoDuration, fps: quickGenerate.videoFps,
                            shots: [{frames: quickGenerate.videoDuration * quickGenerate.videoFps, prompt: prompt,
                                conditions: firstFrame ? [{frame: 0, image: firstFrame, strength: 1}] : []}]})
                    } else jobId = generation.enqueueVideo(prompt, aspectRatio, count, firstFrame,
                        quickGenerate.videoDuration, quickGenerate.videoFps, seed)
                } else if (quickGenerate.hasCanvasInputs || (draft && draft.usesExplicitParameters)) {
                    const parameters = draft ? draft.parameters(generation.selectedModel, true)
                        : quickGenerate.generationParameters(generation.selectedModel)
                    if (Object.keys(parameters).length === 0) {
                        window.generationRequestError = quickGenerate.homePaint.canvas.inputError
                        return
                    }
                    jobId = generation.enqueueHomeCanvas(parameters, aspectRatio, !!draft && draft.usesExplicitParameters)
                } else {
                    jobId = generation.enqueue(prompt, aspectRatio, count, seed)
                }
                if (jobId.length === 0) window.generationRequestError = generation.errorString
            }
        }

        Flickable {
            id: storagePanel
            objectName: "storagePanel"
            visible: !window.useMobileHomeLayout && !window.resultVisible && !window.editorVisible && desktopHome.selectedAction !== "image"
            parent: desktopHome.storageContainer
            width: parent.width
            height: contentHeight
            interactive: false
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
                        ? qsTr("Models and generated media are stored in Society on this device.")
                        : qsTr("Open Society on this device and let it finish syncing your models.")
                }
                LV.LabelMenuButton {
                    id: modelButton
                    objectName: "societyModelSelector"
                    Layout.fillWidth: true
                    text: quickGenerate.mediaType === "Video"
                        ? generation.selectedVideoModel || qsTr("Add an LTX video model in Society")
                        : generation.selectedModel || qsTr("Add a Diffusion model in Society")
                    tone: LV.AbstractButton.Default
                    enabled: quickGenerate.mediaType === "Video" ? generation.videoModels.length > 0 : generation.models.length > 0
                    onClicked: modelMenu.openFor(modelButton, 0, modelButton.height + LV.Theme.gap2)
                }
                LV.LabelMenuButton {
                    id: vaeButton
                    visible: quickGenerate.mediaType !== "Video"
                    objectName: "societyVaeSelector"
                    Layout.fillWidth: true
                    text: generation.selectedVae.length ? qsTr("VAE: ") + generation.selectedVae : qsTr("VAE: Model default")
                    tone: LV.AbstractButton.Default
                    onClicked: vaeMenu.openFor(vaeButton, 0, vaeButton.height + LV.Theme.gap2)
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
                                onClicked: window.showFailureExplanation(queueRow.modelData.id)
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
            id: vaeMenu
            objectName: "societyVaeMenu"
            showIconSlot: false
            itemWidth: Math.max(0, Math.min(440, storagePanel.width - leftPadding - rightPadding - edgeMargin * 2))
            items: [qsTr("Model default")].concat(generation.vaes.map(function(model) { return model.id }))
            selectedIndex: generation.selectedVae.length ? items.indexOf(generation.selectedVae) : 0
            onItemTriggered: function(index, entry) { generation.selectedVae = index === 0 ? "" : String(entry) }
        }
        LV.ContextMenu {
            id: modelMenu
            objectName: "societyModelMenu"
            showIconSlot: false
            itemWidth: Math.max(0, Math.min(440, storagePanel.width - leftPadding - rightPadding - edgeMargin * 2))
            items: (quickGenerate.mediaType === "Video" ? generation.videoModels : generation.models).map(function(model) { return model.id })
            selectedIndex: items.indexOf(quickGenerate.mediaType === "Video" ? generation.selectedVideoModel : generation.selectedModel)
            onItemTriggered: function(index, entry) {
                if (quickGenerate.mediaType === "Video") generation.selectedVideoModel = String(entry)
                else generation.selectedModel = String(entry)
            }
        }
    }
}
