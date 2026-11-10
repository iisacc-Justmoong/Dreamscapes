pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Style 1.0 as Style

LV.HStack {
    id: root
    objectName: "desktopHomeToolbar"
    property alias query: searchField.text
    property var jobs: []
    property bool inlineSearch: false
    property var searchResults: []
    readonly property bool searchActive: searchField.inputItem.activeFocus
    readonly property bool modalActive: accountMenu.visible || notificationMenu.visible
    signal preferencesRequested()
    signal societyRequested()
    signal activityRequested()
    signal searchDismissed()
    signal fileRequested(var file)
    spacing: Style.Tokens.metric("Theme Metrics/Space/8")
    function focusSearch() { searchField.inputItem.forceActiveFocus(); showSearchResults() }
    function showSearchResults() {
        if (!inlineSearch || !searchActive || !query.trim().length) { searchMenu.close(); return }
        const point = searchField.mapToItem(searchMenu.parent, 0, searchField.height + Style.Tokens.metric("Theme Metrics/Space/4"))
        searchMenu.x = Math.max(8, Math.min(point.x, searchMenu.parent.width - searchMenu.width - 8))
        searchMenu.y = point.y
        searchMenu.open()
    }
    function selectSearchResult(direction) {
        showSearchResults()
        const count = searchResults.length
        if (count) searchMenu.selectedIndex = searchMenu.selectedIndex < 0
            ? (direction > 0 ? 0 : count - 1)
            : (searchMenu.selectedIndex + direction + count) % count
    }
    function openSearchResult(index) {
        const result = searchResults[index]
        if (!result) return
        searchMenu.close()
        fileRequested(result.file)
    }
    onInlineSearchChanged: if (!inlineSearch) searchMenu.close()
    onVisibleChanged: if (!visible) { searchMenu.close(); accountMenu.close(); notificationMenu.close() }
    onXChanged: if (searchMenu.visible) showSearchResults()
    onWidthChanged: if (searchMenu.visible) showSearchResults()

    LV.InputField {
        id: searchField
        objectName: "desktopHomeSearch"
        Layout.preferredWidth: Math.min(LV.Theme.scaleMetric(205), Math.max(100, root.width - 60))
        Layout.fillWidth: true
        placeholderText: root.inlineSearch ? qsTr("Search") : qsTr("Search your workspace")
        Accessible.name: qsTr("Search Society files, publications and generation history")
        leadingItems: Image {
            width: 12
            height: 12
            source: LV.Theme.iconPath("inputFieldSearch")
        }
        onTextChanged: {
            root.activityRequested()
            searchMenu.selectedIndex = -1
            root.showSearchResults()
        }
        onAccepted: {
            if (root.inlineSearch && root.query.trim().length) root.openSearchResult(Math.max(0, searchMenu.selectedIndex))
            else if (!root.inlineSearch) root.activityRequested()
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.inlineSearch && root.searchActive && !root.modalActive
        onActivated: { searchMenu.close(); root.searchDismissed() }
    }
    Shortcut { sequence: "Down"; enabled: root.inlineSearch && root.searchActive && root.query.trim().length > 0; onActivated: root.selectSearchResult(1) }
    Shortcut { sequence: "Up"; enabled: root.inlineSearch && root.searchActive && root.query.trim().length > 0; onActivated: root.selectSearchResult(-1) }
    LV.Popover {
        id: searchMenu
        objectName: "desktopSearchResults"
        property int selectedIndex: -1
        focus: false
        // The search shortcut restores canvas focus when dismissing with Escape.
        closePolicy: Controls.Popup.CloseOnPressOutside
        padding: Style.Tokens.metric("Theme Metrics/Space/8")
        width: Math.min(384, parent ? Math.max(0, parent.width - Style.Tokens.metric("Theme Metrics/Space/16")) : root.width)
        implicitHeight: resultsColumn.implicitHeight + topPadding + bottomPadding
        contentItem: LV.VStack {
            id: resultsColumn
            spacing: Style.Tokens.metric("Theme Metrics/Space/2")
            Repeater {
                model: root.searchResults
                LV.MenuItem {
                    id: resultRow
                    required property var modelData
                    required property int index
                    objectName: "desktopSearchResult_" + index
                    Layout.fillWidth: true
                    label: modelData.label
                    compact: true
                    showIconSlot: false
                    keyVisible: false
                    hasChildItems: false
                    tone: LV.AbstractButton.Default
                    state: searchMenu.selectedIndex === index ? selectedState : defaultState
                    onClicked: root.openSearchResult(index)
                    LV.Tooltip {
                        target: resultRow
                        text: resultRow.label
                    }
                }
            }
            LV.Label {
                visible: !root.searchResults.length
                Layout.fillWidth: true
                Layout.preferredHeight: Style.Tokens.metric("Theme Metrics/Size/22")
                text: qsTr("No matching files")
                style: description
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    LV.IconButton {
        id: notifications
        objectName: "desktopNotifications"
        iconSource: LV.Theme.iconPath("toolwindownotifications")
        tone: LV.AbstractButton.Default
        Accessible.name: qsTr("Notifications")
        LV.Tooltip {
            objectName: "desktopNotificationsTooltip"
            target: notifications
            text: notifications.Accessible.name
        }
        onClicked: notificationMenu.openFor(notifications, 0, height + Style.Tokens.metric("Theme Metrics/Space/4"))
    }
    LV.IconButton {
        id: account
        objectName: "desktopAccount"
        iconSource: LV.Theme.iconPath("role-1")
        tone: LV.AbstractButton.Default
        Accessible.name: qsTr("Account")
        LV.Tooltip {
            objectName: "desktopAccountTooltip"
            target: account
            text: account.Accessible.name
        }
        onClicked: accountMenu.openFor(account, 0, height + Style.Tokens.metric("Theme Metrics/Space/4"))
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
        items: NativeData.presentation.notifications(root.jobs)
        onItemTriggered: root.activityRequested()
    }
}
