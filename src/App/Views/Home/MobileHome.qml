pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "mobileHome"
    property var recentFiles: []
    property var recentPublished: []
    property var generationHistory: []
    property bool loading: false
    property string errorText: ""
    readonly property alias quickGenerateContainer: quickGenerateSlot
    signal destinationRequested(int index)
    signal searchRequested()
    signal quickActionRequested(string action)
    signal viewAllRecentFilesRequested()
    signal viewAllPublishedRequested()
    signal viewAllGenerationHistoryRequested()
    signal fileRequested(var file)

    readonly property var destinations: [
        {iconName: "home-1", accessibleName: qsTr("Home")},
        {iconName: "collection", accessibleName: qsTr("Tools")},
        {iconName: "database", accessibleName: qsTr("Storage")},
        {iconName: "toolwindownotifications", accessibleName: qsTr("Notification")},
        {iconName: "role-1", accessibleName: qsTr("Account")}
    ]

    Flickable {
        id: viewport
        objectName: "mobileHomeViewport"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: navigation.top
        contentWidth: width
        contentHeight: content.implicitHeight + LV.Theme.scaleMetric(65)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        LV.VStack {
            id: content
            x: Math.max(LV.Theme.gap16, (viewport.width - width) / 2)
            y: LV.Theme.scaleMetric(33)
            width: Math.max(0, Math.min(LV.Theme.scaleMetric(370), viewport.width - LV.Theme.gap16 * 2))
            height: implicitHeight
            spacing: LV.Theme.gap12

            LV.VStack {
                Layout.fillWidth: true
                spacing: LV.Theme.gap12

                Item {
                    id: quickGenerateSlot
                    objectName: "mobileQuickGenerateSlot"
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                }

                LV.Label {
                    Layout.fillWidth: true
                    Layout.preferredHeight: LV.Theme.scaleMetric(26)
                    text: qsTr("빠른 시작")
                    style: header
                    verticalAlignment: Text.AlignVCenter
                }

                GridLayout {
                    objectName: "mobileQuickActions"
                    Layout.fillWidth: true
                    Layout.preferredHeight: LV.Theme.scaleMetric(220)
                    columns: 2
                    columnSpacing: LV.Theme.gap16
                    rowSpacing: LV.Theme.gap12

                    QuickActionCard {
                        objectName: "newCanvasAction"
                        Layout.fillWidth: true
                        label: qsTr("새 캔버스")
                        iconName: "generaladd"
                        selected: true
                        onClicked: root.quickActionRequested("canvas")
                    }
                    QuickActionCard {
                        objectName: "imageGenerationAction"
                        Layout.fillWidth: true
                        label: qsTr("이미지 생성")
                        iconName: "dreamscapesImage"
                        onClicked: root.quickActionRequested("image")
                    }
                    QuickActionCard {
                        objectName: "videoGenerationAction"
                        Layout.fillWidth: true
                        label: qsTr("비디오 생성")
                        iconName: "dreamscapesVideo"
                        onClicked: root.quickActionRequested("video")
                    }
                    QuickActionCard {
                        objectName: "boardGenerationAction"
                        Layout.fillWidth: true
                        label: qsTr("보드 생성")
                        iconName: "dreamscapesBoard"
                        onClicked: root.quickActionRequested("board")
                    }
                }

                FileCarousel {
                    objectName: "recentFiles"
                    Layout.fillWidth: true
                    title: qsTr("Recent files")
                    files: root.recentFiles
                    maximumItems: 20
                    loading: root.loading
                    emptyText: qsTr("No recent files")
                    cardsObjectName: "recentFileCards"
                    itemObjectNamePrefix: "recentFileCard"
                    onViewAllRequested: root.viewAllRecentFilesRequested()
                    onFileRequested: function(file) { root.fileRequested(file) }
                }
            }

            LV.VStack {
                Layout.fillWidth: true
                spacing: LV.Theme.gap10

                LV.HStack {
                    Layout.fillWidth: true
                    Layout.preferredHeight: LV.Theme.controlHeightSm

                    LV.Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: LV.Theme.gap4
                        text: qsTr("Recent published")
                        style: body
                        elide: Text.ElideRight
                    }
                    LV.LabelButton {
                        objectName: "recentPublishedViewAll"
                        text: qsTr("View all")
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.viewAllPublishedRequested()
                    }
                }

                PublishedList {
                    objectName: "recentPublishedList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: LV.Theme.scaleMetric(280)
                    files: root.recentPublished
                    maximumItems: 4
                    onFileRequested: function(file) { root.fileRequested(file) }
                }

                FileCarousel {
                    objectName: "mobileGenerationHistory"
                    Layout.fillWidth: true
                    title: qsTr("Generation history")
                    files: root.generationHistory
                    maximumItems: 20
                    loading: root.loading
                    emptyText: qsTr("No generated images yet")
                    cardsObjectName: "mobileGenerationHistoryCards"
                    itemObjectNamePrefix: "mobileGenerationHistoryCard"
                    onViewAllRequested: root.viewAllGenerationHistoryRequested()
                    onFileRequested: function(file) { root.fileRequested(file) }
                }
            }
        }
    }

    LV.MobileNavigationBar {
        id: navigation
        objectName: "mobileHomeNavigation"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: implicitHeight
        platformStyle: LV.MobileTab.IOS
        model: root.destinations
        search: ({iconName: "generalsearch", accessibleName: qsTr("Search")})
        currentIndex: 0
        autoSelect: false
        onActivated: function(index) { root.destinationRequested(index) }
        onSearchRequested: root.searchRequested()
    }
}
