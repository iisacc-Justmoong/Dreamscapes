pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Qt.labs.folderlistmodel
import LVRS 1.0 as LV

LV.Modal {
    id: root
    objectName: "preferencesFolderPicker"
    title: qsTr("Choose a Society drive folder")
    showIcon: false
    primaryText: qsTr("Choose")
    secondaryText: qsTr("Cancel")
    primaryEnabled: folders.status === FolderListModel.Ready
    minWidth: LV.Theme.scaleMetric(320)
    maxWidth: LV.Theme.scaleMetric(640)
    verticalOffset: 0
    signal folderSelected(url folder)

    function folderUrl(value) {
        const path = value.trim().replace(/\\/g, "/")
        if (path.startsWith("file:")) return path
        const prefix = path.startsWith("/") ? "file://" : /^[A-Za-z]:\//.test(path) ? "file:///" : ""
        return prefix ? prefix + encodeURI(path).replace(/#/g, "%23").replace(/\?/g, "%3F") : Qt.resolvedUrl(path)
    }
    function showFolder(path) {
        if (path.length > 0) folders.folder = folderUrl(path)
        open = true
    }
    onPrimaryClicked: {
        folderSelected(folders.folder)
        open = false
    }
    onSecondaryClicked: cancel()

    // Filesystem data only; all visible picker components are LVRS.
    FolderListModel {
        id: folders
        objectName: "preferencesFolderModel"
        showFiles: false
        showDirs: true
        showDotAndDotDot: false
        showOnlyReadable: true
    }
    contentComponent: LV.VStack {
        spacing: LV.Theme.gap8
        LV.HStack {
            Layout.fillWidth: true
            spacing: LV.Theme.gap8
            LV.PushButton {
                objectName: "preferencesFolderUp"
                text: qsTr("Up")
                enabled: folders.parentFolder.toString().length > 0 && folders.parentFolder !== folders.folder
                onClicked: folders.folder = folders.parentFolder
            }
            LV.InputField {
                id: pathEntry
                objectName: "preferencesFolderPath"
                Layout.fillWidth: true
                text: folders.folder.toString()
                Accessible.name: qsTr("Folder path")
                onAccepted: folders.folder = root.folderUrl(text)
            }
            Connections {
                target: folders
                function onFolderChanged() { pathEntry.text = folders.folder.toString() }
            }
        }
        PreferenceList {
            id: folderChoices
            objectName: "preferencesFolderList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(LV.Theme.scaleMetric(180), Math.max(LV.Theme.scaleMetric(64), root.height - LV.Theme.scaleMetric(230)))
            minimumListHeight: 0
            footerVisible: false
            enabled: folders.status === FolderListModel.Ready
            model: folders
            labelRole: "fileName"
            listWidth: 0
            itemDelegate: LV.ListItem {
                required property var modelData
                objectName: "preferencesFolderOption" + modelData.index
                type: LV.ListItem.Navigation
                navigationItemWidth: 0
                label: modelData.label || ""
                showDescription: false
                showValue: false
                iconName: "folder@14x14"
                onActiveFocusChanged: if (activeFocus) folderChoices.reveal(this)
                onClicked: folders.folder = modelData.entry.fileUrl
            }
        }
        LV.Label {
            Layout.fillWidth: true
            style: caption
            visible: folders.count === 0
            text: folders.status === FolderListModel.Loading ? qsTr("Loading folders…") : qsTr("No subfolders at this location.")
        }
    }
}
