pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import QtQuick.Layouts
import LVRS 1.0 as LV
import "../Home"

LV.ApplicationWindow {
    id: preferences
    objectName: "preferencesWindow"
    required property var generation
    property var account: null
    property string currentCategory: "General"
    readonly property bool generationCategory: NativeData.presentation.indexOf(["Image", "Video", "Audio", "Agents"], currentCategory) >= 0
    property string locationMessage: ""
    property bool locationFailed: false
    property string driveLocationDraft: generation.containerPath
    signal societyRequested()
    title: qsTr("Preferences — Dreamscapes")
    width: 720
    height: 480
    desktopMinWidth: 560
    desktopMinHeight: 360
    visible: false
    modality: Qt.NonModal
    flags: Qt.Dialog
    useInternalPageStack: false
    navigationEnabled: false
    // Keep the profile row below the native window buttons and LVRS drag strip.
    readonly property real contentTopInset: windowChromeInteractionsEnabled && visibility !== Window.FullScreen
        ? Math.max(windowDragHandleTopMargin + windowDragHandleHeight,
            nativeTitleBarControlsRect.y + nativeTitleBarControlsRect.height) : 0
    function applyDriveLocation() {
        locationFailed = !generation.selectStorageLocation(driveLocationDraft.trim())
        locationMessage = locationFailed ? generation.errorString : qsTr("Society drive location saved.")
    }
    SocietyFolderPicker {
        id: locationDialog
        onFolderSelected: function(folder) { preferences.driveLocationDraft = folder.toString() }
    }
    onVisibleChanged: if (!visible) locationDialog.cancel()
    LV.HStack {
        anchors.fill: parent
        anchors.topMargin: preferences.contentTopInset
        spacing: 0
        PreferenceList {
            id: sidebarScroll
            objectName: "preferencesSidebar"
            Layout.preferredWidth: LV.Theme.scaleMetric(207)
            Layout.minimumWidth: Layout.preferredWidth
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillHeight: true
            backgroundColor: LV.Theme.panelBackground03
            LV.VStack {
                required property var modelData
                spacing: 0
                LV.Spacer { minLength: LV.Theme.scaleMetric(10) }
                LV.VStack {
                    id: sidebarContent
                    Layout.fillWidth: true
                    Layout.leftMargin: LV.Theme.scaleMetric(10)
                    Layout.rightMargin: LV.Theme.scaleMetric(10)
                    spacing: LV.Theme.gap12
                    LV.ListItem {
                        objectName: "preferencesCategoryAccount"
                        Layout.fillWidth: true
                        type: LV.ListItem.Navigation
                        navigationItemWidth: 0
                        label: qsTr("Display Name")
                        description: "@user_id"
                        iconSize: 18
                        iconName: "user"
                        iconSource: preferences.account && preferences.account.hasAvatar ? preferences.account.avatarUrl : ""
                        showValue: false
                        showTrailingIcon: false
                        selected: preferences.currentCategory === "Account"
                        Accessible.name: qsTr("Account")
                        onActiveFocusChanged: if (activeFocus) sidebarScroll.reveal(this)
                        onClicked: preferences.currentCategory = "Account"
                    }
                    Repeater {
                        model: [
                            [ { category: "General", label: qsTr("General"), icon: "settings" },
                              { category: "Appearance", label: qsTr("Appearence"), icon: "stroke" },
                              { category: "Storage", label: qsTr("Storage"), icon: "sqlFile" } ],
                            [ { category: "Image", label: qsTr("Image"), icon: "imageToImage" },
                              { category: "Video", label: qsTr("Video"), icon: "render-preview" },
                              { category: "Audio", label: qsTr("Audio"), icon: "audioClassification" },
                              { category: "Agents", label: qsTr("Agents"), icon: "reinforcementLearning" } ],
                            [ { category: "Share", label: qsTr("Share"), icon: "cwmShare" },
                              { category: "Integration", label: qsTr("Intergration"), icon: "persistenceRelationship" },
                              { category: "Publish", label: qsTr("Publish"), icon: "export" } ],
                            [ { category: "About", label: qsTr("About"), icon: "statusinfo" },
                              { category: "Accessibility", label: qsTr("Accessibility"), icon: "accessMethod" },
                              { category: "KeyboardShortcut", label: qsTr("Keyboard Shortcut"), icon: "keyboard" } ]
                        ]
                        delegate: LV.VStack {
                            id: menuGroup
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            Repeater {
                                model: menuGroup.modelData
                                delegate: LV.MenuItem {
                                    required property var modelData
                                    objectName: "preferencesCategory" + modelData.category
                                    Layout.fillWidth: true
                                    itemWidth: 0
                                    label: modelData.label
                                    iconSize: 18
                                    iconName: modelData.icon
                                    keyVisible: false
                                    showChevron: false
                                    state: preferences.currentCategory === modelData.category ? selectedState : defaultState
                                    Accessible.name: modelData.label
                                    onActiveFocusChanged: if (activeFocus) sidebarScroll.reveal(this)
                                    onClicked: preferences.currentCategory = modelData.category
                                }
                            }
                        }
                    }
                }
                LV.Spacer { minLength: LV.Theme.scaleMetric(10) }
            }
        }
        PreferenceList {
            id: driveDetails
            visible: preferences.currentCategory === "Storage"
            objectName: "preferencesDriveDetails"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            LV.VStack {
                required property var modelData
                width: driveDetails.width
                spacing: LV.Theme.gap16
                LV.Spacer { minLength: LV.Theme.gap8 }
                LV.Label {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    text: qsTr("Storage")
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
                    text: preferences.generation.connected ? preferences.generation.containerPath : qsTr("No Society drive connected")
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
                    text: preferences.driveLocationDraft
                    onTextChanged: if (preferences.driveLocationDraft !== text) preferences.driveLocationDraft = text
                    onAccepted: if (applyLocation.enabled) preferences.applyDriveLocation()
                }
                LV.HStack {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.PushButton {
                        objectName: "browseSocietyDrive"
                        tone: LV.AbstractButton.Default
                        text: qsTr("Choose folder…")
                        enabled: !preferences.generation.busy
                        onClicked: locationDialog.showFolder(locationField.text)
                    }
                    LV.PushButton {
                        id: applyLocation
                        objectName: "applySocietyDrive"
                        text: qsTr("Apply")
                        enabled: locationField.text.trim().length > 0 && !preferences.generation.busy
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
                LV.Spacer { minLength: LV.Theme.gap20 }
            }
        }
        PreferenceList {
            id: generateDetails
            objectName: "preferencesGenerateDetails"
            visible: preferences.generationCategory
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            LV.VStack {
                required property var modelData
                width: generateDetails.width
                spacing: LV.Theme.gap16
                LV.Spacer { minLength: LV.Theme.gap8 }
                LV.Label {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    text: preferences.currentCategory
                    style: header
                }
                LV.Label {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    text: qsTr("Set the defaults used for new generations.")
                    style: description
                    wrapMode: Text.Wrap
                    sizeToContentHeight: true
                }
                LV.VStack {
                    visible: preferences.currentCategory === "Image"
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.Label {
                        text: qsTr("Default image generation model")
                        style: body
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        sizeToContentHeight: true
                    }
                    ModelPreferenceCombo {
                        objectName: "defaultImageGenerationModel"
                        Layout.fillWidth: true
                        models: preferences.generation.models
                        selectedModel: preferences.generation.defaultImageModel
                        settingLabel: qsTr("Default image generation model")
                        onModelSelected: function(id) { preferences.generation.setDefaultImageModel(id) }
                    }
                }
                LV.VStack {
                    visible: preferences.currentCategory === "Video"
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.Label {
                        text: qsTr("Default video generation model")
                        style: body
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        sizeToContentHeight: true
                    }
                    ModelPreferenceCombo {
                        objectName: "defaultVideoGenerationModel"
                        Layout.fillWidth: true
                        models: preferences.generation.videoModels
                        selectedModel: preferences.generation.defaultVideoModel
                        settingLabel: qsTr("Default video generation model")
                        onModelSelected: function(id) { preferences.generation.setDefaultVideoModel(id) }
                    }
                }
                LV.VStack {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.Label {
                        text: qsTr("Default prompt")
                        style: body
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        sizeToContentHeight: true
                    }
                    PromptField {
                        objectName: "defaultGenerationPrompt"
                        maximumLength: 32000
                        clearButtonVisible: false
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        placeholderText: qsTr("Default prompt")
                        Accessible.name: qsTr("Default prompt")
                        text: (preferences.generation.defaultGenerationPrompts[preferences.currentCategory] || {}).prompt || ""
                        onTextEdited: function(value) {
                            preferences.generation.setDefaultGenerationPrompt(preferences.currentCategory, value, false)
                        }
                    }
                }
                LV.VStack {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.Label {
                        objectName: "defaultNegativePromptLabel"
                        text: qsTr("Default negative prompt")
                        style: body
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        sizeToContentHeight: true
                    }
                    PromptField {
                        objectName: "defaultGenerationNegativePrompt"
                        maximumLength: 32000
                        clearButtonVisible: false
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        placeholderText: qsTr("Default negative prompt")
                        Accessible.name: qsTr("Default negative prompt")
                        text: (preferences.generation.defaultGenerationPrompts[preferences.currentCategory] || {}).negativePrompt || ""
                        onTextEdited: function(value) {
                            preferences.generation.setDefaultGenerationPrompt(preferences.currentCategory, value, true)
                        }
                    }
                }
                LV.Label {
                    objectName: "preferencesModelFeedback"
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: preferences.generation.modelPreferencesError
                    textFormat: Text.PlainText
                    style: body
                    color: LV.Theme.accentRed
                    wrapMode: Text.WrapAnywhere
                    sizeToContentHeight: true
                }
                LV.Spacer { minLength: LV.Theme.gap20 }
            }
        }
        LV.VStack {
            id: accountDetails
            objectName: "preferencesAccountDetails"
            visible: preferences.currentCategory === "Account"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: LV.Theme.gap20
            spacing: LV.Theme.gap16
            LV.Label { text: qsTr("Account"); style: header; Layout.fillWidth: true }
            LV.Label {
                text: qsTr("Manage your iisacc account in Society.")
                style: description
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
            LV.PushButton {
                objectName: "preferencesOpenSociety"
                text: qsTr("Open Society")
                onClicked: preferences.societyRequested()
            }
            LV.Spacer {}
        }
        LV.VStack {
            id: categoryDetails
            objectName: "preferencesCategoryDetails"
            visible: preferences.currentCategory !== "Account" && preferences.currentCategory !== "Storage" && !preferences.generationCategory
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: LV.Theme.gap20
            LV.Label {
                objectName: "preferencesCategoryHeading"
                text: preferences.currentCategory === "Appearance" ? qsTr("Appearence")
                    : preferences.currentCategory === "Integration" ? qsTr("Intergration")
                    : preferences.currentCategory === "KeyboardShortcut" ? qsTr("Keyboard Shortcut")
                    : preferences.currentCategory
                style: header
                Layout.fillWidth: true
            }
            LV.Spacer {}
        }
    }
    readonly property Item accountPanelFrame: Frames.PanelFrame { parent: preferences.contentItem; target: accountDetails; panelId: "preferences-account" }
    readonly property Item categoryPanelFrame: Frames.PanelFrame { parent: preferences.contentItem; target: categoryDetails; panelId: "preferences-category" }
    Shortcut { sequence: "Escape"; enabled: preferences.visible; onActivated: locationDialog.open ? locationDialog.cancel() : preferences.close() }
    Shortcut { sequences: [StandardKey.Close]; enabled: preferences.visible; onActivated: preferences.close() }
}
