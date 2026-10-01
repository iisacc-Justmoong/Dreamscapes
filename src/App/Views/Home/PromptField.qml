pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

LV.InputField {
    id: root

    // Keep the LVRS one-line size and padding, adding space for every wrapped line.
    wrapMode: TextInput.Wrap
    centeredTextHeight: contentHeight > Math.ceil(lineMetrics.height)
        ? Math.ceil(contentHeight) : LV.Theme.textBodyLineHeight
    insetVertical: Math.max(0, (fieldMinHeight - LV.Theme.textBodyLineHeight) / 2)

    FontMetrics { id: lineMetrics; font: root.inputItem.font }

    readonly property Flickable scrollViewport: {
        let ancestor = parent
        while (ancestor) {
            const viewport = ancestor as Flickable
            if (viewport && viewport.flickableDirection !== Flickable.HorizontalFlick)
                return viewport
            ancestor = ancestor.parent
        }
        return null
    }

    function keepCursorVisible() {
        const viewport = scrollViewport
        if (!inputItem.activeFocus || !visible || !viewport || viewport.height <= 0)
            return
        const caret = inputItem.cursorRectangle
        const position = inputItem.mapToItem(viewport, caret.x, caret.y)
        const margin = LV.Theme.gap4
        const offset = position.y < margin ? position.y - margin
            : position.y + caret.height > viewport.height - margin
                ? position.y + caret.height - viewport.height + margin : 0
        if (offset !== 0) {
            viewport.contentY = Math.max(0, Math.min(
                Math.max(0, viewport.contentHeight - viewport.height), viewport.contentY + offset))
        }
    }

    function scheduleCursorVisibility() { Qt.callLater(keepCursorVisible) }
    onContentHeightChanged: scheduleCursorVisibility()
    onWidthChanged: scheduleCursorVisibility()
    onHeightChanged: scheduleCursorVisibility()
    onVisibleChanged: scheduleCursorVisibility()
    onScrollViewportChanged: scheduleCursorVisibility()

    Connections {
        target: root.inputItem
        function onCursorRectangleChanged() { root.scheduleCursorVisibility() }
        function onActiveFocusChanged() { root.scheduleCursorVisibility() }
    }
    Connections {
        target: root.scrollViewport
        function onHeightChanged() { root.scheduleCursorVisibility() }
        function onContentHeightChanged() { root.scheduleCursorVisibility() }
    }
}
