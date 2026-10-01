pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.HStack {
    id: root
    objectName: "desktopHomeToolbar"
    property alias query: searchField.text
    property var jobs: []
    signal preferencesRequested()
    signal societyRequested()
    signal activityRequested()
    spacing: LV.Theme.gap8
    function focusSearch() { searchField.inputItem.forceActiveFocus() }

    LV.InputField {
        id: searchField
        objectName: "desktopHomeSearch"
        Layout.preferredWidth: Math.min(LV.Theme.scaleMetric(205), Math.max(100, root.width - 60))
        Layout.fillWidth: true
        placeholderText: qsTr("Search")
        Accessible.name: qsTr("Search Society files, publications and generation history")
        leadingItems: Image {
            width: 12
            height: 12
            source: "Assets/Desktop/search.svg"
        }
        onTextChanged: root.activityRequested()
    }
    LV.IconButton {
        id: notifications
        objectName: "desktopNotifications"
        iconSource: Qt.resolvedUrl("Assets/Desktop/notification.svg")
        tone: LV.AbstractButton.Default
        Accessible.name: qsTr("Notifications")
        Controls.ToolTip.visible: hovered
        Controls.ToolTip.text: qsTr("Notifications")
        onClicked: notificationMenu.openFor(notifications, 0, height + LV.Theme.gap4)
    }
    LV.IconButton {
        id: account
        objectName: "desktopAccount"
        iconSource: Qt.resolvedUrl("Assets/Desktop/account.svg")
        tone: LV.AbstractButton.Default
        Accessible.name: qsTr("Account")
        Controls.ToolTip.visible: hovered
        Controls.ToolTip.text: qsTr("Account")
        onClicked: accountMenu.openFor(account, 0, height + LV.Theme.gap4)
    }
    LV.ContextMenu {
        id: accountMenu
        objectName: "desktopAccountMenu"
        showIconSlot: false
        items: [qsTr("Open Society"), qsTr("Society drive preferences…")]
        onItemTriggered: function(index) {
            if (index === 0) root.societyRequested()
            else root.preferencesRequested()
        }
    }
    LV.ContextMenu {
        id: notificationMenu
        objectName: "desktopNotificationMenu"
        showIconSlot: false
        itemWidth: 260
        items: root.jobs.length ? root.jobs.slice(0, 8).map(function(job) {
            const labels = {queued: qsTr("Queued"), running: qsTr("Generating"), completed: qsTr("Completed"),
                failed: qsTr("Failed"), cancelled: qsTr("Cancelled"), interrupted: qsTr("Interrupted"),
                downloading: qsTr("Downloading"), "connecting-host": qsTr("Connecting")}
            return (labels[job.state] || job.state) + " · " + (job.prompt || qsTr("Generation"))
        }) : [qsTr("No generation notifications")]
        onItemTriggered: root.activityRequested()
    }
}
