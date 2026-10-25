pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import QtQuick.Shapes
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtMultimedia
import LVRS 1.0 as LV
import ".." as Views

Column {
    id: root
    objectName: "editorCameraPhotoPanel"
    required property var toolState
    readonly property var backend: toolState.engine ? toolState.engine.photoSources : null
    property VideoOutput cameraOutput: null
    onCameraOutputChanged: syncPreview()
    readonly property var values: Object.assign({}, NativeData.photo.defaults(), toolState.toolId === "camera-photo" ? toolState.values : toolState.settingsByTool["camera-photo"] || {})
    readonly property string currentViewId: NativeData.photo.views.some(function(view) { return view.id === values.selector }) ? values.selector : "Camera"
    readonly property bool modalActive: options.visible || libraryFolder.visible || regionPicker.visible
    readonly property real inputWidth: Math.min(206, width * 206 / 398)
    readonly property real previewAspectRatio: {
        const parts = String(value("cameraAspect")).split(":")
        const sourceRect = cameraOutput ? cameraOutput.sourceRect : Qt.rect(0, 0, 0, 0)
        let ratio = parts.length === 2 ? Number(parts[0]) / Number(parts[1]) : sourceRect.height > 0 ? sourceRect.width / sourceRect.height : 16 / 9
        if (value("cameraAngle") === "90°" || value("cameraAngle") === "270°") ratio = 1 / ratio
        return ratio
    }
    readonly property var targets: backend ? backend.captureTargets.filter(function(target) { return target.kind === (root.value("captureScope") === "Window" ? "Window" : "Screen") }) : []
    readonly property var captureTarget: targets.find(function(target) { return target.id === root.value("captureTarget") }) || targets[0] || null
    width: 398
    spacing: 12

    function value(id) { return values[id] }
    function asset(name) { return Qt.resolvedUrl("Assets/CameraPhoto/" + name + ".svg") }
    function edit(id, next) {
        if (toolState.toolId !== "camera-photo" || value(id) === next) return
        toolState.setValue(id, next)
    }
    function selectView(id) {
        if (!NativeData.photo.views.some(function(view) { return view.id === id })) return
        edit("selector", id)
        options.close()
    }
    function openOptions(id, control, entries) {
        options.fieldId = id
        options.entries = entries || NativeData.photo.field(id).options.map(function(option) { return {id: option, label: option} })
        options.openFor(control, 0, control.height)
    }
    function syncPreview() {
        if (!backend || !cameraOutput) return
        if (currentViewId === "Camera" && visible) {
            backend.attachPreview(cameraOutput)
            backend.setPreviewEnabled(true, values)
        } else backend.detachPreview(cameraOutput)
    }
    function refreshGallery() {
        if (!backend || !visible || currentViewId !== "Gallery") return
        backend.attachGallery(root)
        backend.loadGallery(value("galleryLibrary"), value("galleryFolder") || "")
        backend.filterGallery(value("gallerySearch"), value("gallerySort"))
    }
    function captureSettings() { return Object.assign({}, values, {captureTarget: captureTarget ? captureTarget.id : ""}) }
    function importSources() {
        if (!backend) return
        if (toolState.generationRuntime && toolState.generationRuntime.containerPath)
            edit("libraryDirectory", toolState.generationRuntime.containerPath)
        if (currentViewId === "Gallery") backend.importSelection()
        else backend.importCurrentCapture()
    }
    function captureScreen() {
        if (!backend) return
        if (value("captureScope") === "Region") {
            if (!captureTarget) return
            regionPicker.target = captureTarget
            regionPicker.show()
            regionPicker.requestActivate()
        } else backend.captureScreen(captureSettings())
    }
    function recordingTime() {
        const seconds = backend ? Math.floor(backend.recordingDuration / 1000) : 0
        return [Math.floor(seconds / 3600), Math.floor(seconds / 60) % 60, seconds % 60].map(function(n) { return String(n).padStart(2, "0") }).join(":")
    }
    onCurrentViewIdChanged: {
        if (backend) backend.clearError()
        syncPreview()
        if (currentViewId === "Capture" && backend) backend.refreshCaptureTargets()
        if (currentViewId === "Gallery") refreshGallery()
        else if (backend) backend.detachGallery(root)
    }
    onVisibleChanged: {
        syncPreview()
        if (visible && currentViewId === "Gallery") refreshGallery()
        else if (backend) backend.detachGallery(root)
    }
    Component.onCompleted: {
        syncPreview()
        if (currentViewId === "Gallery") refreshGallery()
        if (currentViewId === "Capture" && backend) backend.refreshCaptureTargets()
    }
    Component.onDestruction: if (backend) { backend.detachGallery(root); backend.cancel(); if (cameraOutput) backend.detachPreview(cameraOutput) }

    component Heading: Views.PanelRow {
        type: LV.ListItem.Mini; showLeadingIcon: false; minItemWidth: 0; miniItemWidth: 0
        width: parent.width; height: 17
    }
    component Caption: LV.Label {
        style: body; color: LV.Theme.bodyColor
        width: parent.width; height: 18
        lineHeight: 18; elide: Text.ElideRight
    }
    component OptionBox: LV.ComboBox {
        id: option
        required property string fieldId
        objectName: "photoOption-" + fieldId
        text: String(root.value(fieldId))
        height: 20
        tone: LV.AbstractButton.Primary
        arrow: LV.Stepper.UpDown
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: NativeData.photo.field(fieldId).label + ": " + text
        Accessible.onPressAction: option.clicked()
        Keys.onReturnPressed: option.clicked()
        Keys.onSpacePressed: option.clicked()
        onClicked: root.openOptions(fieldId, option)
    }
    component OptionRow: Row {
        id: parameter
        required property string fieldId
        width: parent.width; height: 22; spacing: 8
        Heading { width: parent.width - 105; height: 22; label: NativeData.photo.field(parameter.fieldId).label }
        OptionBox { width: 97; fieldId: parameter.fieldId }
    }
    component ToggleRow: Row {
        id: toggle
        required property string fieldId
        width: parent.width; height: 22; spacing: 8
        Heading { width: parent.width - 46; height: 22; label: NativeData.photo.field(toggle.fieldId).label }
        LV.ToggleSwitch {
            objectName: "photoToggle-" + toggle.fieldId
            trackWidth: 38; trackHeight: 22
            checked: Boolean(root.value(toggle.fieldId))
            Accessible.name: NativeData.photo.field(toggle.fieldId).label
            onToggled: {
                root.edit(toggle.fieldId, checked)
                if (toggle.fieldId === "galleryMultiple" && !checked && root.backend) root.backend.retainSingleSelection()
            }
        }
    }
    component ChoiceGroup: Column {
        id: choices
        required property string fieldId
        width: parent.width; spacing: 8
        Heading { label: NativeData.photo.field(choices.fieldId).label }
        Row {
            spacing: 8
            Repeater {
                model: NativeData.photo.field(choices.fieldId).options
                LV.LabelButton {
                    id: choiceButton
                    required property string modelData
                    required property int index
                    objectName: "photoChoice-" + choices.fieldId + "-" + index
                    text: modelData
                    tone: LV.AbstractButton.Default
                    Accessible.name: NativeData.photo.field(choices.fieldId).label + ": " + modelData
                    Accessible.checkable: true
                    Accessible.checked: root.value(choices.fieldId) === modelData
                    Accessible.onToggleAction: choiceButton.click()
                    EditorButtonState {
                        button: choiceButton
                        selected: root.value(choices.fieldId) === choiceButton.modelData
                    }
                    onClicked: {
                        root.edit(choices.fieldId, modelData)
                        if (choices.fieldId === "captureScope") { root.edit("captureTarget", ""); if (root.backend) root.backend.refreshCaptureTargets() }
                    }
                }
            }
        }
    }
    component TimerRow: Column {
        id: timer
        required property string fieldId
        width: parent.width; spacing: 8
        Heading { label: NativeData.photo.field(timer.fieldId).label }
        Item {
            width: parent.width; height: 22
            Row {
                id: timerChoices
                spacing: Math.min(8, Math.max(2, (root.width - root.inputWidth - 170) / 3))
                Repeater {
                    model: [0, 3, 5, 10]
                    LV.LabelButton {
                        id: timerButton
                        required property int modelData
                        objectName: "photoTimer-" + timer.fieldId + "-" + modelData
                        text: modelData ? modelData + " s" : qsTr("Off")
                        tone: LV.AbstractButton.Default
                        Accessible.name: NativeData.photo.field(timer.fieldId).label + ": " + text
                        Accessible.checkable: true; Accessible.checked: Number(root.value(timer.fieldId)) === modelData
                        Accessible.onToggleAction: timerButton.click()
                        EditorButtonState {
                            button: timerButton
                            selected: Number(root.value(timer.fieldId)) === timerButton.modelData
                        }
                        onClicked: root.edit(timer.fieldId, modelData)
                    }
                }
            }
            LV.InputField {
                objectName: "photoDuration-" + timer.fieldId
                anchors.right: parent.right
                width: Math.min(root.inputWidth, parent.width - timerChoices.implicitWidth - 8); height: 22; fieldMinHeight: 22
                clearButtonVisible: false
                text: [0, 3, 5, 10].indexOf(Number(root.value(timer.fieldId))) >= 0 ? "" : String(root.value(timer.fieldId))
                placeholderText: qsTr("Duration")
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: 60 }
                Accessible.name: NativeData.photo.field(timer.fieldId).label + qsTr(" duration in seconds")
                onTextEdited: { const next = Number(text); if (text.length && isFinite(next) && next >= 0 && next <= 60) root.edit(timer.fieldId, next) }
            }
        }
    }
    component CapturedCard: Rectangle {
        objectName: "photoCurrentCaptured"
        width: parent.width; height: 135; radius: 8
        color: LV.Theme.panelBackground08
        Row {
            x: 8; y: 8; height: 18; width: parent.width - 16; spacing: 8
            Image { width: 18; height: 18; source: root.asset("panel-photo-source"); sourceSize: Qt.size(36, 36) }
            Caption { width: parent.width - 26; text: qsTr("Current captured") }
        }
        Rectangle {
            x: (parent.width - width) / 2; y: 34; width: 138; height: 93
            color: LV.Theme.panelBackground12
            Image {
                objectName: "photoCapturedThumbnail"
                anchors.fill: parent; source: root.backend ? root.backend.captureThumbnail : ""
                fillMode: Image.PreserveAspectFit; asynchronous: true
                sourceSize: Qt.size(552, 372)
            }
            LV.Label {
                visible: !root.backend || !root.backend.captureThumbnail.toString().length
                anchors.centerIn: parent; style: caption; text: root.backend && root.backend.currentCapture.toString().length ? qsTr("Video captured") : qsTr("No capture yet"); color: LV.Theme.captionColor
            }
        }
    }
    // Native/software shape nodes cover the area outside the rounded contour.
    component Corners: Shape {
        id: corners
        anchors.fill: parent
        z: 100
        property real cornerRadius: 8
        property color surround: LV.Theme.surfaceSolid
        ShapePath {
            fillColor: corners.surround; strokeColor: "transparent"
            startX: 0; startY: 0
            PathLine { x: corners.cornerRadius; y: 0 }
            PathQuad { x: 0; y: corners.cornerRadius; controlX: 0; controlY: 0 }
            PathLine { x: 0; y: 0 }
        }
        ShapePath {
            fillColor: corners.surround; strokeColor: "transparent"
            startX: corners.width; startY: 0
            PathLine { x: corners.width; y: corners.cornerRadius }
            PathQuad { x: corners.width - corners.cornerRadius; y: 0; controlX: corners.width; controlY: 0 }
            PathLine { x: corners.width; y: 0 }
        }
        ShapePath {
            fillColor: corners.surround; strokeColor: "transparent"
            startX: corners.width; startY: corners.height
            PathLine { x: corners.width - corners.cornerRadius; y: corners.height }
            PathQuad { x: corners.width; y: corners.height - corners.cornerRadius; controlX: corners.width; controlY: corners.height }
            PathLine { x: corners.width; y: corners.height }
        }
        ShapePath {
            fillColor: corners.surround; strokeColor: "transparent"
            startX: 0; startY: corners.height
            PathLine { x: 0; y: corners.height - corners.cornerRadius }
            PathQuad { x: corners.cornerRadius; y: corners.height; controlX: 0; controlY: corners.height }
            PathLine { x: 0; y: corners.height }
        }
    }
    component ImportRow: Row {
        width: parent.width; height: 22
        Item { width: parent.width - importButton.width; height: 22 }
        LV.LabelButton {
            id: importButton
            objectName: "photoImport-" + root.currentViewId
            text: qsTr("Import"); tone: LV.AbstractButton.Default
            enabled: root.backend && !root.backend.importing && (root.currentViewId === "Gallery" ? root.backend.selectionCount > 0 && !root.backend.galleryBusy : root.backend.currentCapture.toString().length > 0 && !root.backend.busy)
            onClicked: root.importSources()
        }
    }

    LV.IconSegmentedControl {
        objectName: "photoNavigation"
        forceBorderlessTone: false
        Repeater {
            model: NativeData.photo.views
            LV.IconButton {
                id: selectionButton1
                required property var modelData
                objectName: "photoTab-" + modelData.id
                width: 22; height: 22; iconSize: 18
                iconSource: root.asset(modelData.icon)
                tone: LV.AbstractButton.Borderless
                Accessible.name: modelData.id
                Accessible.checkable: true; Accessible.checked: root.currentViewId === modelData.id
                onClicked: root.selectView(modelData.id)

                EditorButtonState { button: selectionButton1; selected: Boolean(root.currentViewId === modelData.id) }
                Accessible.onToggleAction: selectionButton1.click()
            }
        }
    }
    Loader {
        id: cameraContent
        objectName: "photoCameraContent"
        active: root.visible && root.currentViewId === "Camera"
        visible: active
        width: parent.width
        readonly property Item cameraView: item as Item
        height: cameraView ? cameraView.implicitHeight : 0
        sourceComponent: Component {
            Column {
                width: cameraContent.width; spacing: 12
                Binding { target: root; property: "cameraOutput"; value: video; restoreMode: Binding.RestoreBindingOrValue }
                Component.onDestruction: if (root.backend) root.backend.detachPreview(video)
                Rectangle {
                    id: preview
                    objectName: "photoCameraPreview"
                    width: parent.width; height: 144; radius: 8; clip: true
                    color: LV.Theme.panelBackground08
                    Image { anchors.fill: parent; source: "Assets/CameraPhoto/preview-background.png"; fillMode: Image.PreserveAspectCrop }
                    VideoOutput {
                        id: video
                        anchors.fill: parent
                        visible: root.backend && root.backend.previewActive
                        fillMode: VideoOutput.PreserveAspectCrop
                        orientation: parseInt(root.value("cameraAngle"))
                        transform: Scale { origin.x: video.width / 2; xScale: root.value("cameraMirror") ? -1 : 1 }
                    }
                    Column {
                        visible: !video.visible
                        anchors.centerIn: parent; width: parent.width - 32; spacing: 8
                        Image { anchors.horizontalCenter: parent.horizontalCenter; width: 18; height: 18; source: root.asset("preview-camera") }
                        LV.Label { width: parent.width; height: 18; style: body; font.pixelSize: 15; font.weight: Font.Medium; color: LV.Theme.window; text: qsTr("Camera preview"); horizontalAlignment: Text.AlignHCenter }
                        LV.Label { width: parent.width; style: caption; color: LV.Theme.window; text: root.backend ? root.backend.cameraDescription : qsTr("Camera is not attached"); horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                    }
                    TapHandler { onTapped: if (root.backend && !root.backend.busy) root.backend.startPreview(root.values) }
                    Rectangle {
                        visible: video.visible && root.value("cameraGuides")
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 16, (parent.height - 16) * root.previewAspectRatio)
                        height: width / root.previewAspectRatio
                        color: "transparent"; border.width: 1; border.color: LV.Theme.accentWhite; opacity: 0.5
                    }
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            visible: video.visible && root.value("cameraGrid")
                            color: LV.Theme.accentWhite; opacity: 0.35
                            x: index < 2 ? preview.width * (index + 1) / 3 : 0
                            y: index < 2 ? 0 : preview.height * (index - 1) / 3
                            width: index < 2 ? 1 : preview.width
                            height: index < 2 ? preview.height : 1
                        }
                    }
                    LV.Label {
                        visible: root.backend && root.backend.countdown > 0
                        anchors.centerIn: parent; style: title; text: root.backend ? String(root.backend.countdown) : ""
                        color: LV.Theme.accentWhite
                    }
                    Corners {}
                }
                Column {
                    width: parent.width; spacing: 8
                    Row {
                        width: parent.width; height: 17
                        Heading { width: parent.width / 2; label: qsTr("Device") }
                        LV.Label { width: parent.width / 2; height: 17; style: body; color: root.backend && root.backend.previewActive ? LV.Theme.success : LV.Theme.captionColor; text: root.backend ? root.backend.cameraStatus : qsTr("Unavailable"); horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                    }
                    LV.ComboBox {
                        id: deviceBox
                        objectName: "photoDevice"
                        width: parent.width; height: 20
                        text: root.backend ? (root.backend.devices.find(function(device) { return device.id === (root.value("cameraDevice") || root.backend.deviceId) }) || {label: qsTr("No camera connected")}).label : qsTr("No camera connected")
                        tone: LV.AbstractButton.Primary; arrow: LV.Stepper.UpDown
                        enabled: root.backend && !root.backend.busy && root.backend.devices.length > 0
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Camera device")
                        Accessible.onPressAction: deviceBox.clicked()
                        Keys.onReturnPressed: deviceBox.clicked()
                        Keys.onSpacePressed: deviceBox.clicked()
                        onClicked: root.openOptions("cameraDevice", deviceBox, root.backend.devices)
                    }
                    Row {
                        width: parent.width; height: 22
                        Item { width: parent.width - refresh.width; height: 22 }
                        LV.LabelButton { id: refresh; objectName: "photoRefreshDevices"; text: qsTr("Refresh devices"); tone: LV.AbstractButton.Default; onClicked: if (root.backend) root.backend.refreshDevices() }
                    }
                }
                OptionRow { fieldId: "cameraAspect" }
                OptionRow { fieldId: "cameraAngle" }
                TimerRow { fieldId: "cameraTimer" }
                ChoiceGroup { fieldId: "cameraDrive" }
                ToggleRow { fieldId: "cameraGuides" }
                ToggleRow { fieldId: "cameraGrid" }
                ToggleRow { fieldId: "cameraMirror" }
                ToggleRow { fieldId: "cameraSavePhotos" }
                Row {
                    width: parent.width; height: 22; spacing: 8
                    LV.LabelButton { objectName: "photoCapturePhoto"; text: qsTr("Capture photo"); tone: LV.AbstractButton.Default; enabled: root.backend && root.backend.cameraAvailable && !root.backend.busy; onClicked: root.backend.capturePhoto(root.values) }
                    LV.LabelButton { id: videoButton; objectName: "photoCaptureVideo"; text: root.backend && root.backend.recording ? qsTr("Stop video") : qsTr("Capture video"); tone: LV.AbstractButton.Default; enabled: root.backend && root.backend.cameraAvailable && (!root.backend.busy || root.backend.recording); onClicked: root.backend.toggleVideo(root.values) }
                    LV.Label { width: Math.max(0, parent.width - x); height: 22; style: body; text: root.recordingTime(); color: LV.Theme.danger; horizontalAlignment: Text.AlignRight; visible: root.backend && root.backend.recording }
                }
                CapturedCard {}
                ImportRow {}
            }
        }
    }
    Column {
        visible: root.currentViewId === "Capture"
        width: parent.width; spacing: 12
        Row {
            width: parent.width; height: 18; spacing: 8
            Image { width: 18; height: 18; source: root.asset("transform-selection") }
            LV.Label { width: parent.width - 26; height: 18; style: header; text: qsTr("Capture") }
        }
        Caption { height: root.width < 330 ? 36 : 18; wrapMode: Text.WordWrap; elide: Text.ElideNone; text: qsTr("Capture a display, an app window, or a precise region.") }
        Rectangle {
            objectName: "photoScreenPreview"
            width: parent.width; height: 144; radius: 8; color: LV.Theme.panelBackground08
            Column {
                anchors.centerIn: parent; width: parent.width - 32; spacing: 8
                Image { anchors.horizontalCenter: parent.horizontalCenter; width: 18; height: 18; source: root.asset("transform-selection") }
                LV.Label { width: parent.width; height: 18; style: body; font.pixelSize: 15; font.weight: Font.Medium; text: root.backend && root.backend.countdown > 0 ? qsTr("Capture in %1 s").arg(root.backend.countdown) : qsTr("Screen capture"); horizontalAlignment: Text.AlignHCenter }
                LV.Label { width: parent.width; style: caption; text: root.captureTarget ? root.captureTarget.label + " · " + (root.captureTarget.pixelWidth || root.captureTarget.width) + " × " + (root.captureTarget.pixelHeight || root.captureTarget.height) : qsTr("Choose a display or window"); horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
            }
        }
        ChoiceGroup { fieldId: "captureScope" }
        Column {
            width: parent.width; spacing: 8
            Heading { label: qsTr("Display / window") }
            LV.ComboBox {
                id: targetBox
                objectName: "photoCaptureTarget"
                width: parent.width; height: 20
                text: root.captureTarget ? root.captureTarget.label : qsTr("Choose a target")
                tone: LV.AbstractButton.Primary; arrow: LV.Stepper.UpDown
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Display or window")
                Accessible.onPressAction: targetBox.clicked()
                Keys.onReturnPressed: targetBox.clicked()
                Keys.onSpacePressed: targetBox.clicked()
                onClicked: {
                    if (root.backend) root.backend.refreshCaptureTargets()
                    root.openOptions("captureTarget", targetBox, root.targets)
                }
            }
        }
        TimerRow { fieldId: "captureDelay" }
        ToggleRow { fieldId: "capturePointer" }
        ToggleRow { fieldId: "captureFullResolution" }
        ToggleRow { fieldId: "captureTransparent" }
        Row {
            width: parent.width; height: 22
            LV.LabelButton { objectName: "photoCaptureImage"; text: qsTr("Capture image"); tone: LV.AbstractButton.Default; enabled: root.backend && root.backend.screenCaptureSupported && !root.backend.busy; onClicked: root.captureScreen() }
        }
        CapturedCard {}
        ImportRow {}
    }
    Column {
        visible: root.currentViewId === "Gallery"
        width: parent.width; spacing: 12
        Row {
            width: parent.width; height: 22; spacing: 8
            Image { width: 18; height: 18; source: root.asset("panel-photo-library") }
            LV.Label { width: parent.width - 26; height: 22; style: header; text: qsTr("Gallery") }
        }
        Caption { height: root.width < 300 ? 36 : 18; wrapMode: Text.WordWrap; elide: Text.ElideNone; text: qsTr("Browse, organize, and reuse your image sources.") }
        Column {
            width: parent.width; spacing: 8
            Heading { label: qsTr("Library") }
            OptionBox { fieldId: "galleryLibrary"; width: parent.width }
        }
        LV.InputField {
            mode: searchMode
            objectName: "photoGallerySearch"
            width: parent.width; height: 22; fieldMinHeight: 22
            text: root.value("gallerySearch"); placeholderText: qsTr("Search")
            Accessible.name: qsTr("Search photos")
            onTextChanged: {
                root.edit("gallerySearch", text)
                if (root.backend) root.backend.filterGallery(text, root.value("gallerySort"))
            }
        }
        Row {
            width: parent.width; height: 22
            Heading { width: parent.width - sortButton.width; height: 22; label: qsTr("Sort by") }
            LV.DropdownButton {
                id: sortButton
                objectName: "photoGallerySort"
                iconSource: root.asset("sort-assets"); iconSize: 18
                iconMode: true; tone: LV.AbstractButton.Default
                Accessible.name: qsTr("Sort photos: ") + root.value("gallerySort")
                onClicked: root.openOptions("gallerySort", sortButton)
            }
        }
        Item {
            width: parent.width; height: 216
            GridView {
                id: galleryGrid
                objectName: "photoGalleryGrid"
                anchors.fill: parent
                clip: true; cellWidth: (width + 8) / 2; cellHeight: 112
                model: root.backend ? root.backend.galleryModel : null
                boundsBehavior: Flickable.StopAtBounds
                Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: tile
                    required property var entry
                    required property int index
                    objectName: "photoGalleryTile-" + index
                    width: galleryGrid.cellWidth - 8; height: 104; radius: 8; clip: true
                    color: LV.Theme.panelBackground08
                    border.width: entry.selected ? 2 : 0; border.color: LV.Theme.accent
                    Image { anchors.fill: parent; anchors.margins: tile.border.width; source: tile.entry.thumbnail || ""; asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize: Qt.size(400, 208) }
                    Image { visible: !tile.entry.thumbnail.toString().length; anchors.centerIn: parent; width: 18; height: 18; source: root.asset("panel-photo-source") }
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: entry.label
                    Accessible.description: entry.previewError || ""
                    Accessible.checkable: true; Accessible.checked: entry.selected
                    Accessible.onToggleAction: if (root.backend) root.backend.setSelected(entry.id, root.value("galleryMultiple"))
                    TapHandler { onTapped: if (root.backend) root.backend.setSelected(tile.entry.id, root.value("galleryMultiple")) }
                    LV.Tooltip {
                        target: tile
                        text: tile.entry.label
                    }
                    Corners {}
                    Component.onCompleted: if (root.backend) root.backend.requestThumbnail(entry.id)
                }
            }
            Column {
                visible: !galleryGrid.count
                anchors.centerIn: parent; width: parent.width - 32; spacing: 8
                LV.Label {
                    width: parent.width; style: body
                    text: root.backend ? root.backend.galleryStatus || qsTr("No images available") : qsTr("Photo library is not attached")
                    wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; sizeToContentHeight: true
                }
                LV.LabelButton {
                    objectName: "photoGalleryAuthorize"
                    visible: root.backend && root.backend.galleryAuthorizationRequired
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Allow Photos access"); tone: LV.AbstractButton.Default
                    onClicked: if (root.backend) root.backend.requestGalleryAccess()
                }
            }
        }
        Row {
            width: parent.width; height: 22
            Caption { width: parent.width - selectAll.width; height: 22; text: qsTr("%1 selected").arg(root.backend ? root.backend.selectionCount : 0) }
            LV.LabelButton { id: selectAll; objectName: "photoSelectAll"; text: qsTr("Select all"); tone: LV.AbstractButton.Default; enabled: root.backend && galleryGrid.count > 0; onClicked: { root.edit("galleryMultiple", true); root.backend.selectAll(true) } }
        }
        ToggleRow { fieldId: "galleryMultiple" }
        ImportRow {}
        LV.Label { visible: root.backend && galleryGrid.count > 0 && root.backend.galleryStatus.length > 0; width: parent.width; style: caption; text: root.backend ? root.backend.galleryStatus : ""; wrapMode: Text.WordWrap; sizeToContentHeight: true }
    }
    LV.ContextMenu {
        id: options
        objectName: "photoOptions"
        property string fieldId: "cameraAspect"
        property var entries: []
        showIconSlot: false
        items: entries.map(function(entry) { return {label: entry.label} })
        onItemTriggered: function(index) {
            if (!entries[index]) return
            const next = entries[index].id
            if (fieldId === "galleryLibrary" && next === "Choose folder…") { libraryFolder.open(); return }
            root.edit(fieldId, next)
            if (fieldId === "cameraDevice") root.syncPreview()
            if (fieldId === "galleryLibrary") root.refreshGallery()
            if (fieldId === "gallerySort" && root.backend) root.backend.filterGallery(root.value("gallerySearch"), next)
        }
    }
    FolderDialog {
        id: libraryFolder
        objectName: "photoGalleryFolder"
        title: qsTr("Choose an image folder")
        onAccepted: { root.edit("galleryFolder", selectedFolder); root.edit("galleryLibrary", "Choose folder…"); root.refreshGallery() }
    }
    EditorCaptureRegion {
        id: regionPicker
        onSelected: function(rect) {
            const settings = root.captureSettings()
            settings.captureRegion = [rect.x, rect.y, rect.width, rect.height]
            // Allow the selector window to disappear before the native screenshot.
            captureRegionTimer.settings = settings
            captureRegionTimer.start()
        }
    }
    Timer {
        id: captureRegionTimer
        property var settings: ({})
        interval: 250
        onTriggered: if (root.backend) root.backend.captureScreen(settings)
    }
    LV.Label {
        objectName: "photoStatus"
        visible: text.length > 0
        width: parent.width; style: caption; wrapMode: Text.WordWrap; sizeToContentHeight: true
        text: root.backend && root.backend.importError.length ? root.backend.importError : root.backend && root.backend.error.length ? root.backend.error : root.toolState.notice
    }
}

