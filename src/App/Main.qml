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

LV.ApplicationWindow {
    id: window
    objectName: "mainWindow"
    property var preferencesWindow: null
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
        LV.ApplicationWindow {
            id: preferences
            objectName: "preferencesWindow"
            title: qsTr("Preferences — Dreamscapes")
            transientParent: window
            width: 720
            height: 440
            desktopMinWidth: 360
            desktopMinHeight: 320
            visible: false
            modality: Qt.NonModal
            flags: Qt.Dialog
            useInternalPageStack: false
            navigationEnabled: false
            property string locationMessage: ""
            property bool locationFailed: false
            function applyDriveLocation() {
                locationFailed = !(generation.selectStorageLocation(locationField.text.trim()))
                locationMessage = locationFailed ? generation.errorString
                    : qsTr("Society drive location saved.")
            }
            FolderDialog {
                id: locationDialog
                title: qsTr("Choose an existing Society drive folder")
                onAccepted: locationField.text = selectedFolder.toString()
            }
            RowLayout {
                anchors.fill: parent
                spacing: 0
                Rectangle {
                    Layout.preferredWidth: Math.min(180, preferences.width * 0.3)
                    Layout.fillHeight: true
                    color: LV.Theme.panelBackground03
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: LV.Theme.gap12
                        spacing: LV.Theme.gap16
                        LV.Label { text: qsTr("Preferences"); style: header2; Layout.fillWidth: true }
                        LV.PushButton {
                            objectName: "preferencesDriveCategory"
                            text: qsTr("Society drive")
                            Layout.fillWidth: true
                            tone: LV.AbstractButton.Primary
                            Accessible.name: qsTr("Society drive category")
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
                Controls.ScrollView {
                    id: driveDetails
                    objectName: "preferencesDriveDetails"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: driveDetails.availableWidth
                        spacing: LV.Theme.gap16
                        Item { Layout.preferredHeight: LV.Theme.gap8 }
                        LV.Label {
                            Layout.leftMargin: LV.Theme.gap20
                            Layout.rightMargin: LV.Theme.gap20
                            Layout.fillWidth: true
                            text: qsTr("Society drive")
                            style: header
                        }
                        LV.Label {
                            Layout.leftMargin: LV.Theme.gap20
                            Layout.rightMargin: LV.Theme.gap20
                            Layout.fillWidth: true
                            text: qsTr("Choose the existing Society drive used for shared models and files. This does not move or delete data.")
                            style: description
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                        }
                        LV.Label {
                            Layout.leftMargin: LV.Theme.gap20
                            Layout.rightMargin: LV.Theme.gap20
                            text: qsTr("Current location")
                            style: header2
                        }
                        LV.Label {
                            objectName: "preferencesCurrentDrive"
                            style: body
                            Layout.leftMargin: LV.Theme.gap20
                            Layout.rightMargin: LV.Theme.gap20
                            Layout.fillWidth: true
                            text: generation.connected ? generation.containerPath : qsTr("No Society drive connected")
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            sizeToContentHeight: true
                        }
                        LV.InputField {
                            id: locationField
                            objectName: "preferencesDriveLocation"
                            Layout.leftMargin: LV.Theme.gap20
                            Layout.rightMargin: LV.Theme.gap20
                            Layout.fillWidth: true
                            placeholderText: qsTr("Existing Society drive folder")
                            Accessible.name: qsTr("Society drive location")
                            text: generation.containerPath
                            onAccepted: if (applyLocation.enabled) preferences.applyDriveLocation()
                        }
                        Flow {
                            Layout.leftMargin: LV.Theme.gap20
                            Layout.rightMargin: LV.Theme.gap20
                            Layout.fillWidth: true
                            spacing: LV.Theme.gap8
                            LV.PushButton {
                                objectName: "browseSocietyDrive"
                                tone: LV.AbstractButton.Default
                                text: qsTr("Choose folder…")
                                enabled: !generation.busy
                                onClicked: locationDialog.open()
                            }
                            LV.PushButton {
                                id: applyLocation
                                objectName: "applySocietyDrive"
                                text: qsTr("Apply")
                                enabled: locationField.text.trim().length > 0 && !generation.busy
                                onClicked: preferences.applyDriveLocation()
                            }
                        }
                        LV.Label {
                            objectName: "preferencesDriveFeedback"
                            style: body
                            Layout.leftMargin: LV.Theme.gap20
                            Layout.rightMargin: LV.Theme.gap20
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: preferences.locationMessage
                            color: preferences.locationFailed ? LV.Theme.accentRed : LV.Theme.textTokenBody
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            sizeToContentHeight: true
                        }
                        Item { Layout.preferredHeight: LV.Theme.gap20 }
                    }
                }
            }
            Shortcut { sequence: "Escape"; enabled: preferences.visible; onActivated: preferences.close() }
            Shortcut { sequences: [StandardKey.Close]; enabled: preferences.visible; onActivated: preferences.close() }
        }
    }
    Shortcut {
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
                objectName: "globalPreferencesAction"
                text: qsTr("Preferences…")
                role: Platform.MenuItem.PreferencesRole
                onTriggered: window.openPreferences()
            }
            Platform.MenuItem {
                text: qsTr("Quit Dreamscapes")
                role: Platform.MenuItem.QuitRole
                onTriggered: Qt.quit()
            }
        }
        Platform.Menu { title: qsTr("Edit") }
        Platform.Menu { title: qsTr("Window") }
        Platform.Menu { title: qsTr("Help") }
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
    property url canvasImageSource: ""
    property var canvasSpecification: ({})
    property var canvasGenerationResult: ({})
    property var currentResult: ({})
    property var resultJobIds: []
    property string generationRequestError: ""
    readonly property alias generationBackend: generation
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
    // Keep the app-owned title-bar row and every view clear of AppKit's native
    // traffic lights. The extra content gap prevents the first control row from
    // visually colliding with either the buttons or the drag region.
    readonly property real desktopTitleBarHeight: LV.Theme.scaleMetric(48)
    readonly property real desktopTitleBarContentGap: LV.Theme.gap8
    readonly property real desktopTitleBarLeadingClearance: nativeTitleBarControlsRect.width > 0
        ? nativeTitleBarControlsRect.x + nativeTitleBarControlsRect.width + LV.Theme.gap12
        : LV.Theme.gap12
    readonly property real contentTopInset: Math.max(mobileSystemSafeTopInset,
        windowChromeInteractionsEnabled && windowDragHandleEnabled && visibility !== Window.FullScreen
            ? Math.max(0, windowDragHandleTopMargin + windowDragHandleHeight) : 0,
        !useMobileHomeLayout
            ? (visibility === Window.FullScreen
                ? (!resultVisible && !editorVisible ? LV.Theme.controlHeightSm + LV.Theme.gap12 : 0)
                : desktopTitleBarHeight + desktopTitleBarContentGap)
            : 0)
    title: "Dreamscapes"
    primaryColor: LV.Theme.defaultPrimary
    width: useMobileHomeLayout ? 390 : 1280
    height: useMobileHomeLayout ? 844 : 800
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
    windowDragHandleHeight: 40
    windowDragExclusionItems: [appContent, desktopToolbar]

    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)
    signal newProjectRequested(url imageSource, var generationResult)

    function openCanvas(imageSource, generationResult) {
        quickGenerate.dismissInput()
        modelMenu.close()
        if (!imageSource) editorView.createBlankCanvas({width: 1024, height: 1024, unit: "px", ppi: 300, background: "White", name: qsTr("Untitled Canvas")})
        canvasImageSource = imageSource || ""
        canvasGenerationResult = generationResult || ({})
        canvasSpecification = ({})
        editorVisible = true
        editorView.forceActiveFocus()
    }
    function closeCanvas() {
        editorVisible = false
    }
    function openNewCanvas() {
        quickGenerate.dismissInput()
        modelMenu.close()
        newCanvasDialog.begin()
    }

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
        editorVisible = false
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
        query: window.useMobileHomeLayout ? "" : desktopToolbar.query
    }
    SocietyApplication { id: societyApplication }

    GenerationController {
        id: generation
        objectName: "generationController"
        onSubmissionQueued: function(jobIds) {
            const firstJob = generation.jobs.find(job => job.id === jobIds[0])
            // Reference images use an advanced recipe, but Home still owns the result route.
            if (quickGenerate.submitting || !firstJob || !firstJob.advancedParameters)
                window.showSubmission(jobIds)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: LV.Theme.surfaceSolid
        visible: !window.useMobileHomeLayout && !window.resultVisible && !window.editorVisible
    }
    DesktopHomeToolbar {
        id: desktopToolbar
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Math.max(0, (window.desktopTitleBarHeight - height) / 2)
        anchors.rightMargin: LV.Theme.gap12
        width: Math.min(LV.Theme.scaleMetric(265), Math.max(0,
            window.width - anchors.rightMargin - window.desktopTitleBarLeadingClearance))
        visible: !window.useMobileHomeLayout && !window.resultVisible && !window.editorVisible
        jobs: generation.jobs
        onPreferencesRequested: window.openPreferences()
        onSocietyRequested: window.openSociety()
        onActivityRequested: desktopHome.showHome()
    }
    Shortcut {
        sequence: StandardKey.Find
        enabled: desktopToolbar.visible && !newCanvasDialog.visible
        onActivated: desktopToolbar.focusSearch()
    }
    Shortcut { sequence: StandardKey.New; enabled: !newCanvasDialog.visible; onActivated: window.openNewCanvas() }
    NewCanvasDialog {
        id: newCanvasDialog
        availableArea: appContent
        onCanvasRequested: function(specification) {
            if (!editorView.createBlankCanvas(specification)) {
                creationError = qsTr("Could not create the canvas. Adjust its dimensions and try again.")
                return
            }
            window.canvasSpecification = specification
            window.canvasImageSource = ""
            window.canvasGenerationResult = ({})
            window.editorVisible = true
            close()
            editorView.forceActiveFocus()
        }
    }
    function openSociety() {
        window.generationRequestError = Qt.openUrlExternally("society://")
            ? "" : qsTr("Could not open Society. Open Society on this device and try again.")
    }
    function openHomeFile(file) {
        if (file.previewSource && file.previewSource.toString().length > 0) window.openCanvas(file.previewSource, file)
        else window.openSociety()
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
            visible: window.useMobileHomeLayout && !window.resultVisible && !window.editorVisible
            onQuickActionRequested: function(action) {
                if (action === "canvas") window.openNewCanvas()
            }
            recentFiles: historyModel.recentFiles
            recentPublished: historyModel.recentPublished
            generationHistory: historyModel.generationHistory
            loading: historyModel.loading
            errorText: historyModel.errorString
            onViewAllGenerationHistoryRequested: {
                const opened = societyApplication.openGenerationHistory()
                if (!opened) console.warn(qsTr("Could not open Society. Open Society on this device and try again."))
            }
        }

        DesktopHome {
            id: desktopHome
            anchors.fill: parent
            visible: !window.useMobileHomeLayout && !window.resultVisible && !window.editorVisible
            recentFiles: historyModel.recentFiles
            recentPublished: historyModel.recentPublished
            generationHistory: historyModel.generationHistory
            loading: historyModel.loading
            errorText: historyModel.errorString
            onQuickActionRequested: function(action) {
                if (action === "canvas") window.openNewCanvas()
                else if (action === "tools") window.openCanvas("", ({}))
                else if (action === "image") {
                    imageWorkspaceLoader.opened = true
                    if (imageWorkspaceLoader.item) imageWorkspaceLoader.item.parameterPanel.focusPrompt()
                    window.generationRequestError = ""
                } else if (action === "video") {
                    quickGenerate.mediaType = "Video"
                    quickGenerate.focusPrompt()
                    window.generationRequestError = ""
                } else {
                    window.generationRequestError = action === "audio"
                        ? qsTr("Audio generation is not available yet. Your prompt is preserved.")
                        : qsTr("Board creation is not available yet. Your prompt is preserved.")
                }
            }
            onBrowseRequested: window.openSociety()
            onPublishedRequested: window.openSociety()
            onHistoryRequested: {
                if (!societyApplication.openGenerationHistory())
                    window.generationRequestError = qsTr("Could not open Society. Open Society on this device and try again.")
            }
            onFileRequested: function(file) { window.openHomeFile(file) }
            onPromptRequested: function(prompt) { quickGenerate.prompt = prompt; quickGenerate.focusPrompt() }
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
            }
        }

        GenerationResult {
            id: resultView
            anchors.top: parent.top
            anchors.bottom: window.resultVisible
                ? resultComposerViewport.top : parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            visible: window.resultVisible && !window.editorVisible
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
                window.openCanvas(imageSource, generationResult)
                window.newProjectRequested(imageSource, generationResult)
            }
        }

        CanvasEditor {
            id: editorView
            mobileLayout: window.useMobileHomeLayout
            anchors.fill: parent
            visible: window.editorVisible
            imageSource: window.canvasImageSource
            generationResult: window.canvasGenerationResult
            canvasSpecification: window.canvasSpecification
            onBackRequested: window.closeCanvas()
        }

        Flickable {
            id: resultComposerViewport
            objectName: "resultComposerViewport"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: window.resultVisible && !window.editorVisible
            // Preserve access to Back when a prompt grows beyond the window.
            height: Math.min(quickGenerate.implicitHeight,
                Math.max(0, parent.height - LV.Theme.controlHeightSm * 2))
            contentWidth: width
            contentHeight: quickGenerate.implicitHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        }

        QuickGenerate {
            id: quickGenerate
            visible: !window.editorVisible && (window.resultVisible || window.useMobileHomeLayout || desktopHome.selectedAction !== "image")
            parent: window.resultVisible ? resultComposerViewport.contentItem : window.useMobileHomeLayout
                ? mobileHome.quickGenerateContainer : desktopHome.quickGenerateContainer
            y: 0
            x: 0
            width: parent ? parent.width : 0
            height: implicitHeight
            contentInset: window.resultVisible ? LV.Theme.gap10 : 0
            canvasEnabled: !window.useMobileHomeLayout && !window.resultVisible
            menusOpenUpward: window.resultVisible
            errorText: window.resultVisible ? "" : window.generationRequestError
            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                window.generateRequested(prompt, mediaType, aspectRatio, count)
                window.generationRequestError = ""
                if (mediaType === "Video") {
                    window.generationRequestError = qsTr("Video generation is not available yet. Your prompt is preserved; choose Image to generate an image.")
                    return
                }
                let jobId
                if (quickGenerate.hasCanvasInputs) {
                    const parameters = quickGenerate.generationParameters(generation.selectedModel)
                    if (Object.keys(parameters).length === 0) {
                        window.generationRequestError = quickGenerate.homePaint.canvas.inputError
                        return
                    }
                    jobId = generation.enqueueHomeCanvas(parameters, aspectRatio)
                } else {
                    jobId = generation.enqueue(prompt, aspectRatio, count)
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
                LV.LabelMenuButton {
                    id: vaeButton
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
            items: generation.models.map(function(model) { return model.id })
            selectedIndex: items.indexOf(generation.selectedModel)
            onItemTriggered: function(index, entry) { generation.selectedModel = String(entry) }
        }
    }
}
